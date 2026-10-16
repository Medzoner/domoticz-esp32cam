#pragma once

namespace app::core::routes {

inline constexpr char kIndex[] = "/";
inline constexpr char kHealth[] = "/health";
inline constexpr char kStatus[] = "/status";
inline constexpr char kCapture[] = "/capture";
inline constexpr char kControl[] = "/control";
inline constexpr char kStopStreams[] = "/stream-stop";
inline constexpr char kRecordingStart[] = "/recording/start";
inline constexpr char kRecordingStop[] = "/recording/stop";
inline constexpr char kRecordings[] = "/recordings";
inline constexpr char kRecordingDelete[] = "/recording/delete";
inline constexpr char kProvisioningScan[] = "/scan";
inline constexpr char kProvisioningSave[] = "/save";
inline constexpr char kQueryVariable[] = "var";
inline constexpr char kQueryValue[] = "val";
inline constexpr char kQueryId[] = "id";

}  // namespace app::core::routes
