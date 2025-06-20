var gulp = require('gulp');
var clean = require('gulp-clean');
var cleanCSS = require('gulp-clean-css');
var htmlmin = require('gulp-html-minifier-terser');
var terser = require('gulp-terser');
var concat = require('gulp-concat');
var useref = require('gulp-useref');
var gulpif = require('gulp-if');
var browserSync = require('browser-sync').create();

// Clean dist folder
gulp.task('clean', function () {
    return gulp.src('dist', { allowEmpty: true, read: false })
        .pipe(clean());
});

// Minify CSS
gulp.task('css', function () {
    return gulp.src('app/styles/**/*.css')
        .pipe(cleanCSS())
        .pipe(gulp.dest('dist/styles'));
});

// Process HTML with useref and minify
gulp.task('html', function () {
    return gulp.src('app/**/*.html')
        .pipe(useref())
        .pipe(gulpif('*.js', terser()))
        .pipe(gulpif('*.css', cleanCSS()))
        .pipe(gulpif('*.html', htmlmin({ collapseWhitespace: true })))
        .pipe(gulp.dest('dist'));
});

// Minify JS
gulp.task('js', function () {
    return gulp.src('app/scripts/**/*.js')
        .pipe(terser())
        .pipe(gulp.dest('dist/scripts'));
});

// Serve files
gulp.task('serve', function () {
    browserSync.init({
        server: {
            baseDir: 'app',
            routes: {
                '/node_modules': 'node_modules'
            }
        },
        port: 9000
    });

    gulp.watch('app/styles/**/*.css').on('change', browserSync.reload);
    gulp.watch('app/scripts/**/*.js').on('change', browserSync.reload);
    gulp.watch('app/**/*.html').on('change', browserSync.reload);
});

// Build task
gulp.task('build', gulp.series('clean', 'css', 'js', 'html'));

// Default task
gulp.task('default', gulp.series('build', 'serve'));
