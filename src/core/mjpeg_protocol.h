#pragma once

#include <cstddef>

namespace app::core::mjpeg {

inline constexpr char kRoute[] = "/stream";
inline constexpr char kBoundaryName[] = "frame-boundary";
inline constexpr char kBoundary[] = "\r\n--frame-boundary\r\n";
inline constexpr char kContentType[] =
    "multipart/x-mixed-replace;boundary=frame-boundary";
inline constexpr char kSuccessResponse[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: multipart/x-mixed-replace;boundary=frame-boundary\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Cache-Control: no-store, no-cache, must-revalidate\r\n"
    "Pragma: no-cache\r\n"
    "Expires: 0\r\n"
    "Connection: close\r\n\r\n";
inline constexpr char kNotFoundResponse[] =
    "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
inline constexpr char kUnavailableResponse[] =
    "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n";

bool isStreamRequestLine(const char* requestLine);
std::size_t formatPartPreamble(char* destination, std::size_t capacity,
                               std::size_t jpegSize);

}  // namespace app::core::mjpeg
