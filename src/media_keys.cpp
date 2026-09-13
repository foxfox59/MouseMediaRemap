#include "media_keys.hpp"

#include <windows.h>

const wchar_t* MediaActionName(MediaAction action) {
  switch (action) {
    case MediaAction::None: return L"(None)";
    case MediaAction::PreviousTrack: return L"Previous track";
    case MediaAction::PlayPause: return L"Play/Pause";
    case MediaAction::NextTrack: return L"Next track";
    case MediaAction::Stop: return L"Stop";
    case MediaAction::Mute: return L"Mute";
    case MediaAction::VolumeUp: return L"Volume up";
    case MediaAction::VolumeDown: return L"Volume down";
    default: return L"(None)";
  }
}

MediaAction MediaActionFromName(const std::wstring& name) {
  for (int i = 0; i < static_cast<int>(MediaAction::Count); ++i) {
    auto a = static_cast<MediaAction>(i);
    if (name == MediaActionName(a)) {
      return a;
    }
  }
  return MediaAction::None;
}

static void TapKey(WORD vk) {
  INPUT inputs[2]{};
  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = vk;
  inputs[1].type = INPUT_KEYBOARD;
  inputs[1].ki.wVk = vk;
  inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(2, inputs, sizeof(INPUT));
}

void SendMediaAction(MediaAction action) {
  switch (action) {
    case MediaAction::PreviousTrack: TapKey(VK_MEDIA_PREV_TRACK); break;
    case MediaAction::PlayPause: TapKey(VK_MEDIA_PLAY_PAUSE); break;
    case MediaAction::NextTrack: TapKey(VK_MEDIA_NEXT_TRACK); break;
    case MediaAction::Stop: TapKey(VK_MEDIA_STOP); break;
    case MediaAction::Mute: TapKey(VK_VOLUME_MUTE); break;
    case MediaAction::VolumeUp: TapKey(VK_VOLUME_UP); break;
    case MediaAction::VolumeDown: TapKey(VK_VOLUME_DOWN); break;
    case MediaAction::None:
    default:
      break;
  }
}