/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos.
 **
 **  Calaos is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#include "JpegThumbnail.h"

#include "config.h"

#ifndef HAVE_LIBJPEG

namespace JpegThumbnail
{

Result resize(const std::string &, int)
{
    return {Status::Unavailable, {}};
}

}

#else

#include <algorithm>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <jpeglib.h>
#include <jerror.h>

using std::string;

namespace
{

// Far above any camera, far below what 16 bit SOF fields can announce.
const JDIMENSION maxSide = 16384;
const uint64_t maxPixels = 64000000;
// Only progressive and multi-scan files reach it: they buffer every coefficient.
const long maxDecoderMemory = 128L * 1024 * 1024;
// A file of a few KiB can otherwise ask for thousands of full-image passes.
const int maxScans = 64;
const int maxWarnings = 100;
const int quality = 75;

/* setjmp lives in convert(), one frame below the owner of this struct: a
 * longjmp must never cross an object with a destructor. The output buffer is
 * sized up front, so no libjpeg callback ever allocates or throws. */
struct Job
{
    jpeg_decompress_struct decoder;
    jpeg_compress_struct encoder;
    jpeg_error_mgr errors;
    jpeg_progress_mgr progress;
    jpeg_destination_mgr output;
    jmp_buf jump;
    int warnings;
    bool outputFull;
    string thumbnail;
};

[[noreturn]] void abortJob(j_common_ptr info)
{
    longjmp(static_cast<Job *>(info->client_data)->jump, 1);
}

void onMessage(j_common_ptr info, int level)
{
    if (level >= 0)
        return;

    // libjpeg pads a truncated stream with grey and calls that a warning.
    Job *job = static_cast<Job *>(info->client_data);
    if (info->err->msg_code == JWRN_JPEG_EOF || ++job->warnings > maxWarnings)
        abortJob(info);
}

void onProgress(j_common_ptr info)
{
    if (reinterpret_cast<j_decompress_ptr>(info)->input_scan_number > maxScans)
        abortJob(info);
}

void outputUnused(j_compress_ptr)
{
}

boolean outputExhausted(j_compress_ptr encoder)
{
    static_cast<Job *>(encoder->client_data)->outputFull = true;
    abortJob(reinterpret_cast<j_common_ptr>(encoder));
}

bool acceptable(const jpeg_decompress_struct &decoder)
{
    if (decoder.image_width == 0 || decoder.image_height == 0)
        return false;
    if (decoder.image_width > maxSide || decoder.image_height > maxSide)
        return false;
    if (uint64_t(decoder.image_width) * decoder.image_height > maxPixels)
        return false;
    return decoder.num_components == 1 || decoder.num_components == 3;
}

template <typename T>
T *allocate(Job &job, size_t count)
{
    // The pool frees it on any exit, and reports failure through abortJob.
    auto *pool = reinterpret_cast<j_common_ptr>(&job.decoder);
    return static_cast<T *>(job.decoder.mem->alloc_large(pool, JPOOL_IMAGE, count * sizeof(T)));
}

JDIMENSION scaledHeight(JDIMENSION sourceWidth, JDIMENSION sourceHeight, JDIMENSION width)
{
    uint64_t height = (uint64_t(sourceHeight) * width + sourceWidth / 2) / sourceWidth;
    return JDIMENSION(std::clamp<uint64_t>(height, 1, sourceHeight));
}

void writeAverages(Job &job, uint64_t *sums, const uint32_t *columnsPerCell, JSAMPROW row,
                   JDIMENSION width, JDIMENSION rows)
{
    for (JDIMENSION cell = 0; cell < width; cell++)
    {
        uint64_t pixels = uint64_t(columnsPerCell[cell]) * rows;
        for (int channel = 0; channel < 3; channel++)
        {
            uint64_t &sum = sums[cell * 3 + channel];
            row[cell * 3 + channel] = JSAMPLE((sum + pixels / 2) / pixels);
            sum = 0;
        }
    }
    jpeg_write_scanlines(&job.encoder, &row, 1);
}

JpegThumbnail::Status convert(Job &job, const string &jpeg, int width)
{
    using JpegThumbnail::Status;

    if (setjmp(job.jump))
        return job.outputFull ? Status::Unchanged : Status::Failed;

    jpeg_decompress_struct &decoder = job.decoder;
    jpeg_compress_struct &encoder = job.encoder;

    jpeg_create_decompress(&decoder);
    jpeg_create_compress(&encoder);
    decoder.mem->max_memory_to_use = maxDecoderMemory;
    decoder.progress = &job.progress;

    // Older headers take a non-const pointer. libjpeg never writes through it.
    jpeg_mem_src(&decoder, reinterpret_cast<unsigned char *>(const_cast<char *>(jpeg.data())),
                 jpeg.size());
    jpeg_read_header(&decoder, TRUE);

    if (!acceptable(decoder))
        return Status::Failed;

    const JDIMENSION targetWidth = JDIMENSION(width);
    if (decoder.image_width <= targetWidth)
        return Status::Unchanged;

    // The DCT scaler does nearly all the work for a fraction of the cost:
    // it only has to stay wider than the target, the box filter does the rest.
    decoder.out_color_space = JCS_RGB;
    decoder.scale_num = 1;
    decoder.scale_denom = 1;
    for (unsigned int denom : {8u, 4u, 2u})
    {
        if (decoder.image_width / denom >= targetWidth)
        {
            decoder.scale_denom = denom;
            break;
        }
    }

    jpeg_start_decompress(&decoder);

    const JDIMENSION sourceWidth = decoder.output_width;
    const JDIMENSION sourceHeight = decoder.output_height;
    const JDIMENSION targetHeight = scaledHeight(sourceWidth, sourceHeight, targetWidth);

    encoder.dest = &job.output;
    encoder.image_width = targetWidth;
    encoder.image_height = targetHeight;
    encoder.input_components = 3;
    encoder.in_color_space = JCS_RGB;
    jpeg_set_defaults(&encoder);
    jpeg_set_quality(&encoder, quality, TRUE);
    jpeg_start_compress(&encoder, TRUE);

    JSAMPROW sourceRow = allocate<JSAMPLE>(job, size_t(sourceWidth) * 3);
    JSAMPROW targetRow = allocate<JSAMPLE>(job, size_t(targetWidth) * 3);
    JDIMENSION *cellOfColumn = allocate<JDIMENSION>(job, sourceWidth);
    uint32_t *columnsPerCell = allocate<uint32_t>(job, targetWidth);
    uint64_t *sums = allocate<uint64_t>(job, size_t(targetWidth) * 3);

    std::fill_n(columnsPerCell, targetWidth, 0);
    std::fill_n(sums, size_t(targetWidth) * 3, 0);
    for (JDIMENSION x = 0; x < sourceWidth; x++)
    {
        cellOfColumn[x] = JDIMENSION(uint64_t(x) * targetWidth / sourceWidth);
        columnsPerCell[cellOfColumn[x]]++;
    }

    JDIMENSION currentRow = 0;
    JDIMENSION rowsInCell = 0;
    for (JDIMENSION y = 0; y < sourceHeight; y++)
    {
        jpeg_read_scanlines(&decoder, &sourceRow, 1);

        JDIMENSION row = JDIMENSION(uint64_t(y) * targetHeight / sourceHeight);
        if (row != currentRow)
        {
            writeAverages(job, sums, columnsPerCell, targetRow, targetWidth, rowsInCell);
            currentRow = row;
            rowsInCell = 0;
        }

        for (JDIMENSION x = 0; x < sourceWidth; x++)
            for (JDIMENSION channel = 0; channel < 3; channel++)
                sums[cellOfColumn[x] * 3 + channel] += sourceRow[x * 3 + channel];
        rowsInCell++;
    }
    writeAverages(job, sums, columnsPerCell, targetRow, targetWidth, rowsInCell);

    jpeg_finish_compress(&encoder);

    // Deliberately no jpeg_finish_decompress: it would parse the rest of the
    // file for a picture that is already complete.
    job.thumbnail.resize(job.thumbnail.size() - job.output.free_in_buffer);
    return Status::Resized;
}

}

namespace JpegThumbnail
{

Result resize(const string &jpeg, int width)
{
    if (jpeg.empty() || width < 1)
        return {Status::Failed, {}};

    Job job{};

    // Past the source size the thumbnail is pointless: outputExhausted stops
    // there, which is also what bounds the encoder.
    job.thumbnail.resize(jpeg.size());
    job.output.next_output_byte = reinterpret_cast<JOCTET *>(job.thumbnail.data());
    job.output.free_in_buffer = job.thumbnail.size();
    job.output.init_destination = outputUnused;
    job.output.empty_output_buffer = outputExhausted;
    job.output.term_destination = outputUnused;

    job.progress.progress_monitor = onProgress;

    // Both must be set before jpeg_create_*, which zeroes everything else.
    job.decoder.err = jpeg_std_error(&job.errors);
    job.errors.error_exit = abortJob;
    job.errors.emit_message = onMessage;
    job.decoder.client_data = &job;
    job.encoder.err = &job.errors;
    job.encoder.client_data = &job;

    Status status = convert(job, jpeg, width);

    // Both are safe on a zeroed or half-built object.
    jpeg_destroy_compress(&job.encoder);
    jpeg_destroy_decompress(&job.decoder);

    if (status == Status::Resized)
        return {status, std::move(job.thumbnail)};
    return {status, {}};
}

}

#endif
