/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#ifndef JPEG_TEST_FRAMES_H
#define JPEG_TEST_FRAMES_H

#include "config.h"

#ifdef HAVE_LIBJPEG

#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include <jpeglib.h>

// Synthetic frames for the camera thumbnail tests, and the means to break them.
namespace JpegTestFrames
{

using Pixel = std::function<void(int x, int y, unsigned char *rgb)>;

struct Options
{
    int quality = 85;
    bool progressive = false;
    bool optimize = false;
    J_COLOR_SPACE color = JCS_RGB;
    // Full-precision grey scans: repeating one makes a valid, silent stream.
    bool plainScans = false;
};

inline std::string encode(int width, int height, const Pixel &pixel, const Options &options = {})
{
    jpeg_compress_struct encoder;
    jpeg_error_mgr errors;
    encoder.err = jpeg_std_error(&errors);
    jpeg_create_compress(&encoder);

    unsigned char *buffer = nullptr;
    unsigned long size = 0;
    jpeg_mem_dest(&encoder, &buffer, &size);

    const int components = options.color == JCS_GRAYSCALE ? 1 : options.color == JCS_CMYK ? 4
                                                                                          : 3;
    encoder.image_width = width;
    encoder.image_height = height;
    encoder.input_components = components;
    encoder.in_color_space = options.color;
    jpeg_set_defaults(&encoder);
    jpeg_set_quality(&encoder, options.quality, TRUE);
    encoder.optimize_coding = options.optimize ? TRUE : FALSE;

    jpeg_scan_info plainScript[2] = {};
    if (options.plainScans)
    {
        plainScript[0].comps_in_scan = 1;
        plainScript[1].comps_in_scan = 1;
        plainScript[1].Ss = 1;
        plainScript[1].Se = 63;
        encoder.scan_info = plainScript;
        encoder.num_scans = 2;
    }
    else if (options.progressive)
    {
        jpeg_simple_progression(&encoder);
    }

    jpeg_start_compress(&encoder, TRUE);

    std::vector<JSAMPLE> row(size_t(width) * components);
    for (int y = 0; y < height; y++)
    {
        unsigned char rgb[3] = { 0, 0, 0 };
        for (int x = 0; x < width; x++)
        {
            pixel(x, y, rgb);
            for (int c = 0; c < components; c++)
                row[size_t(x) * components + c] = c < 3 ? rgb[c] : 255;
        }
        JSAMPROW rows[1] = { row.data() };
        jpeg_write_scanlines(&encoder, rows, 1);
    }

    jpeg_finish_compress(&encoder);
    jpeg_destroy_compress(&encoder);

    std::string jpeg(reinterpret_cast<char *>(buffer), size);
    std::free(buffer);
    return jpeg;
}

inline std::string solid(int width, int height, const Options &options = {})
{
    return encode(
        width, height, [](int, int, unsigned char *rgb)
        {
        rgb[0] = 90;
        rgb[1] = 140;
        rgb[2] = 200;
        },
        options);
}

struct Decoded
{
    int width = 0;
    int height = 0;
    int components = 0;
    std::vector<unsigned char> pixels;

    const unsigned char *at(int x, int y) const
    {
        return pixels.data() + (size_t(y) * width + x) * components;
    }
};

struct CheckedErrors
{
    jpeg_error_mgr errors;
    std::jmp_buf jump;
};

inline void failDecode(j_common_ptr info)
{
    std::longjmp(reinterpret_cast<CheckedErrors *>(info->err)->jump, 1);
}

// width stays 0 when the bytes are not a whole JPEG.
inline Decoded decode(const std::string &jpeg)
{
    Decoded result;
    jpeg_decompress_struct decoder;
    CheckedErrors checked;
    decoder.err = jpeg_std_error(&checked.errors);
    checked.errors.error_exit = failDecode;
    jpeg_create_decompress(&decoder);

    if (setjmp(checked.jump))
    {
        jpeg_destroy_decompress(&decoder);
        return {};
    }

    jpeg_mem_src(&decoder, reinterpret_cast<const unsigned char *>(jpeg.data()), jpeg.size());
    jpeg_read_header(&decoder, TRUE);
    jpeg_start_decompress(&decoder);

    result.width = decoder.output_width;
    result.height = decoder.output_height;
    result.components = decoder.output_components;
    result.pixels.resize(size_t(result.width) * result.height * result.components);
    while (decoder.output_scanline < decoder.output_height)
    {
        JSAMPROW rows[1] = { result.pixels.data() + size_t(decoder.output_scanline) * result.width * result.components };
        jpeg_read_scanlines(&decoder, rows, 1);
    }

    jpeg_finish_decompress(&decoder);
    jpeg_destroy_decompress(&decoder);
    return result;
}

inline size_t segmentLength(const std::string &jpeg, size_t marker)
{
    return (size_t(uint8_t(jpeg[marker + 2])) << 8) | uint8_t(jpeg[marker + 3]);
}

// Position of the first marker in [first, last], jpeg.size() when there is none.
inline size_t findSegment(const std::string &jpeg, unsigned char first, unsigned char last)
{
    size_t position = 2;
    while (position + 4 <= jpeg.size())
    {
        unsigned char marker = uint8_t(jpeg[position + 1]);
        if (marker >= first && marker <= last)
            return position;
        position += 2 + segmentLength(jpeg, position);
    }
    return jpeg.size();
}

// A few bytes are enough to announce gigapixels: nothing else in the file changes.
inline std::string withDimensions(std::string jpeg, unsigned width, unsigned height)
{
    size_t sof = findSegment(jpeg, 0xC0, 0xC2);
    jpeg[sof + 5] = char(height >> 8);
    jpeg[sof + 6] = char(height & 0xFF);
    jpeg[sof + 7] = char(width >> 8);
    jpeg[sof + 8] = char(width & 0xFF);
    return jpeg;
}

// Plays the first scan of a multi-scan file `times` more times.
inline std::string withRepeatedFirstScan(const std::string &jpeg, int times)
{
    size_t start = findSegment(jpeg, 0xDA, 0xDA);
    size_t end = start + 2 + segmentLength(jpeg, start);
    while (end + 1 < jpeg.size())
    {
        unsigned char next = uint8_t(jpeg[end + 1]);
        bool stuffed = next == 0x00 || (next >= 0xD0 && next <= 0xD7);
        if (uint8_t(jpeg[end]) == 0xFF && !stuffed)
            break;
        end++;
    }

    std::string scan = jpeg.substr(start, end - start);
    std::string repeated = jpeg.substr(0, end);
    for (int i = 0; i < times; i++)
        repeated += scan;
    return repeated + jpeg.substr(end);
}

} //namespace JpegTestFrames

#endif
#endif
