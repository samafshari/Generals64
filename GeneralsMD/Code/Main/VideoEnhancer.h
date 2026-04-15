#pragma once
// VideoEnhancer: First-run video upscaling using ffmpeg.
//
// On startup, checks if enhanced videos exist. If ffmpeg is available
// and videos haven't been processed yet, shows a progress dialog and upscales
// each video file using Lanczos scaling with NVENC hardware encoding.
//
// Enhanced videos are stored alongside originals with a different extension
// and the video player preferentially loads them.

#include <windows.h>

namespace VideoEnhancer
{
    // Returns the path to the enhanced version of a video file.
    // e.g., "Data\\Movies\\intro.bik" -> "Data\\Movies_Enhanced\\intro.mp4"
    void GetEnhancedPath(const char* originalPath, char* outPath, int outPathSize);

    // Check if an enhanced version exists for the given original path.
    bool HasEnhancedVideo(const char* originalPath);

    // Check if ffmpeg.exe is available.
    bool AreToolsAvailable();

    // Run the full enhancement pipeline with a progress dialog.
    // Shows a window with progress bar. Processes all .bik files in Data\Movies.
    // Returns true if any videos were processed (or all already done).
    // hInstance: application instance for creating the dialog window.
    // This function blocks until processing is complete or cancelled.
    bool EnhanceAllVideos(HINSTANCE hInstance);
}
