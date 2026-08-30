# Speech survey findings

The evidence behind [SPEECH.md](SPEECH.md), from two consulted reviews
(UX practice in capture apps; platform integration paths), Aug 2026.
SPEECH.md holds the decisions; this holds the facts and the sources.

## UX facts

Apps surveyed: Apple Notes, iOS system dictation, Google Keep, Drafts,
Bear, Todoist, Voice Memos, WhatsApp/Telegram (recording conventions only).

- Apple Notes and Bear ship no in-app mic: keyboard dictation is their whole
  story. Drafts and Keep add their own mic for session control, capture
  without focusing a field, mid-session language switching, and one behavior
  across keyboards. Keep also stores the audio beside the transcript.
- Endpoint timings: Android's recognizer ends an utterance after roughly
  1 to 2 s of silence, and the EXTRA_SPEECH_INPUT_*_SILENCE extras are
  widely ignored by the modern Google recognizer. iOS keyboard dictation
  stays live and quits after about 30 s of silence; a live SFSpeechRecognizer
  session caps near 60 s. Flutter apps conventionally set pauseFor near 3 s.
  Drafts defaults capture to manual stop, silence timeout opt-in.
- Auto-commit: voice search runs the query on the final result in Google,
  YouTube and Spotify. No notes app auto-saves dictated capture; the one
  fire-and-forget path is the assistant, always with a confirmation toast
  naming the saved note.
- iOS dictation styles unconfirmed words with a light highlight, plays
  distinct start and stop sounds, and adds a haptic on start.
- Press-hold, slide-to-cancel, swipe-lock (WhatsApp, Telegram) belong to
  audio recording, not dictation. Dictation is tap-to-toggle.
- Spoken structure fails in shipping apps: Todoist quick-add works by voice
  only for dates, since #project and @label are unspeakable; "hashtag work"
  dictates ambiguously and no major notes app ships in-utterance commands.
  Assistants parse "add milk to shopping list" server-side.
- Error placement: inline at the mic. Denied permission gets an Open Settings
  deep link (openSettingsURLString on iOS, the app-details intent on
  Android); Gboard's offline-pack download prompt is the pack-missing model;
  Google's no-speech wording is "Didn't hear that".
- Screen readers: announce listening start, stop and the final transcript
  once; partials in a live region choke TalkBack. Targets 48 dp Material,
  44 pt HIG.

Sources: [Drafts dictation docs](https://docs.getdrafts.com/docs/editor/dictation),
[Apple: dictate text on iPhone](https://support.apple.com/guide/iphone/dictate-text-iph2c0651d2/ios),
[iOS dictation 30 s silence](https://www.yaps.ai/blog/apple-dictation-stops-after-30-seconds),
[Assistant notes moved to Keep](https://alternativeto.net/news/2023/11/google-assistant-to-remove-note-and-list-system-and-move-it-to-google-keep-instead),
[Android endpointer guide](https://medium.com/@andraz.pajtler/android-speech-to-text-the-missing-guide-part-1-824e2636c45a).

## UX recommendations, as ranked by the consultant

1. Search auto-runs, capture confirm-first: dictated value lands editable
   with a visible Save; auto-committing capture is nonstandard outside
   assistants.
2. Two endpoint profiles: about 2 s pauseFor for search, 3 s or more for
   capture with a permanent stop, since composing pauses run longer.
3. One listening visual everywhere: accent-tinted pulsing mic, a level cue,
   and the word "Listening"; users learn one state signal per app.
4. Make the missing-pack error a one-tap fix: per-locale wording, a settings
   deep-link button, and one clause that audio never leaves the device.
5. Tags by chips, not grammar: recent-tag chips plus a dictatable field
   split on commas and "and".
6. Permission-denied and no-speech messages inline at the mic, with Open
   Settings and one-tap retry; never toasts.
7. Haptic on listen start and stop; dictation is used eyes-free.
8. Dim unconfirmed partials in the capture editor, as iOS dictation does.
9. 48 dp targets; announce only start, stop and the final transcript to
   screen readers.
10. An App Shortcut / App Action later as the one legitimate fire-and-forget
    path, with a confirmation toast naming the saved note.

## Platform recommendations, as sequenced by the consultant

1. First, iOS App Intents + App Shortcuts, Swift calling the C engine
   directly; the recall intent in the same pass.
2. Second, the capture deep link + app shortcut + auto-dictation route,
   shared by both platforms; the share-sheet receiver in the same pass.
3. Optional third, the home-screen mic widget over that route.
4. Skip App Actions (Gemini ignores them); wait on AppFunctions; skip
   wrapping SpeechAnalyzer unless transcription quality draws complaints.

One recommendation rejected: fall back to networked recognition where
on-device is unavailable. The product promise is on-device or refuse, and
the pack-missing message is the fallback.

## Platform facts

- SiriKit's notes domain (INCreateNoteIntent) deprecated since iOS 15; WWDC
  2026 deprecated SiriKit entirely. App Intents is the sole Siri path, App
  Shortcuts need iOS 16, App Intents 2.0 lands in iOS 27. A shortcut phrase
  must contain the app name and cannot carry free text; Siri collects the
  text through the parameter's requestValueDialog. The intent's perform()
  runs without launching the app, so Swift can call the C engine directly
  (it is already linked into the Runner): no MethodChannel, no headless Dart.
  Cost 2 to 4 days, about 150 lines of Swift.
- Android App Actions (CREATE_NOTE capability in shortcuts.xml): docs still
  live, but Gemini replaced Assistant through 2026 and does not recognize
  App Actions, so apps that relied on them lost the feature. The successor
  is AppFunctions (androidx.appfunctions, Android 16+), a private preview as
  of May 2026 (Uber, DoorDash, Samsung Gallery). "Open <app>" by voice
  survived the transition, which is why a deep link into a hot-mic capture
  screen is the realistic Android hands-free path (a day of work).
- Every App Intent appears in the iOS Shortcuts app for free; Drafts and
  Bear ship "Dictate text -> create note" chains, Action Button and back-tap
  includable, as their hands-free story. Share-sheet capture is the
  highest-usage capture path in Bear, Simplenote and Keep, 1 to 2 days both
  platforms.
- On-device recognition: iOS requiresOnDeviceRecognition since iOS 13, per
  locale and device, queryable via supportsOnDeviceRecognition. iOS 26 adds
  SpeechAnalyzer/SpeechTranscriber, fully on-device, reportedly about 2x
  faster than Whisper Large V3 Turbo; no Flutter plugin wraps it. Android
  createOnDeviceSpeechRecognizer is API 31+ with isOnDeviceRecognitionAvailable,
  packs reliable on Pixel and variable elsewhere. The speech_to_text plugin
  maps onDevice: true to both as best-effort and exposes neither
  availability check.

Sources: [deprecated SiriKit domains](https://developer.apple.com/support/deprecated-sirikit-intent-domains),
[SiriKit to App Intents, iOS 27](https://byteiota.com/sirikit-app-intents-migration-ios-27/),
[App Actions docs](https://developer.android.com/develop/devices/assistant/overview),
[AppFunctions overview](https://developer.android.com/ai/appfunctions),
[Assistant to Gemini 2026](https://9to5google.com/2025/12/19/google-assistant-gemini-2026/),
[Gemini ignoring app shortcuts](https://discuss.ai.google.dev/t/how-to-invoke-custom-dynamic-shortcuts-and-app-functions-with-gemini/103215),
[WWDC25 SpeechAnalyzer](https://developer.apple.com/videos/play/wwdc2025/277/),
[speech_to_text](https://pub.dev/packages/speech_to_text).
