# Speech support

Voice as a first-class input: speak to recall (GET) and speak to file (PUT).
[ROADMAP.md](../ROADMAP.md) names the goal; this doc owns the design; the
surveyed evidence and sources are in [SPEECH_SURVEY.md](SPEECH_SURVEY.md). Flutter
has no speech UI of its own (no dictation widget, no listening state, no
recognition API; `speech_to_text` is a community plugin that returns
transcripts), so the in-app interaction is designed once here, the same on
Android and iOS, taking from Apple's and Android's patterns where each is good.
Platform code exists only where the OS owns the entry point: permissions,
offline packs, and the assistants.

The engine needs nothing. `embed.h` add/recall is the contract; every item
below is shell work.

## What exists

- One seam: the mic in the search field (`app/flutter/lib/main.dart`,
  `_listen`), mobile only. Partials fill the box as they arrive; the final
  result runs recall. Auto-running voice search is the industry standard
  (Google, YouTube, Spotify), so this stays.
- On-device only: `SpeechListenOptions(onDevice: true)`, because the plugin
  default streams audio to the platform's cloud recognizer. A device without
  an offline language pack is told to install one. No cloud fallback, ever;
  the privacy answers on both stores promise it.
- Permissions and manifests done on both platforms (`RECORD_AUDIO` plus the
  Android 11+ `RecognitionService` query; mic and speech usage strings on iOS).
- Untested on hardware. Emulators have no recognizer; the acceptance list in
  [issue #1](https://github.com/Anode1/ais/issues/1) covers the first real run.

## The standard, from apps that do this

Surveyed: Apple Notes, iOS dictation, Google Keep, Drafts (the capture
reference), Todoist, Voice Memos.

- Three entry tiers ship: the system keyboard mic (free in every text field,
  Apple Notes and Bear add nothing on top), an in-app mic (Drafts, Keep: for
  session control, capture without focusing a field first, one behavior across
  keyboards), and assistant intents (fire-and-forget, always followed by a
  confirmation toast naming what was saved).
- Search auto-runs on the final result. Capture never auto-commits: the
  transcript lands in an editable buffer and the user presses Save (Drafts,
  Keep). Only assistants skip the confirmation.
- Live partials are universal; showing nothing until the final result reads as
  broken. iOS dims unconfirmed words. Listening state is a change on the mic
  itself (accent tint, pulse) plus the word "Listening"; a haptic marks start
  and stop, since dictation is used eyes-free. Tap toggles; a stop control
  stays visible.
- Endpointing: about 2 s of silence ends a search utterance; capture needs 3 s
  or more plus the visible stop, because composing pauses are longer (Drafts
  defaults capture to manual stop).
- Errors sit inline where the mic is, never in a toast. Denied permission gets
  an Open Settings button that deep-links. A missing offline pack names the
  fix for the locale, buttons into settings, and states in one clause that
  audio never leaves the device. Silence gets "Didn't catch that" with the mic
  still armed.
- Spoken tag grammars do not ship outside assistants: Todoist's `#project`
  symbols are unspeakable, "hashtag" dictates ambiguously. What works is
  capture first, organize after: a chip row of recent tags plus a text field
  that can itself be dictated, split on commas and "and".
- Accessibility: 48 dp targets, announce listening start, stop and the final
  transcript only (partials flood a screen reader), and the typed path always
  exists.

## Entry points the OS owns

| Path | Status | Cost | Verdict |
| --- | --- | --- | --- |
| iOS App Intents + App Shortcuts ("Hey Siri, add a note in AIS") | the standard; SiriKit deprecated, App Intents the sole path, iOS 16 floor | about 150 lines of Swift; `perform()` calls the C engine already linked in the Runner, no Dart bridge; Siri asks "What should it say?" itself | build |
| iOS Shortcuts app | every App Intent appears there; users chain Dictate text into Add note (how Drafts and Bear do hands-free) | free with the above | comes along |
| Android App Actions (`CREATE_NOTE` capability) | Gemini replaced Assistant through 2026 and ignores App Actions | medium, payoff gone | skip |
| Android AppFunctions | the successor, Android 16+, private preview | Kotlin over JNI to the same engine, later | wait |
| `ais://capture` deep link + static app shortcut | "Gemini, open AIS" still works; link opens capture with the mic hot: the realistic Android hands-free story | small, shared route | build |
| Home-screen widget with a mic (Keep's pattern) | taps into the same capture route | native widget per platform | later |

On-device recognition underneath: iOS `SFSpeechRecognizer` with
`requiresOnDeviceRecognition` (iOS 13+, per locale and device), Android
`createOnDeviceSpeechRecognizer` (API 31+, packs vary by OEM outside Pixel).
The plugin maps `onDevice: true` to both, best-effort, and exposes no
availability check, which is why the pack-missing message matters.

## What to add, in order

1. Prove GET on hardware (issue #1), then polish it to the standard above:
   listening state on the mic, haptic on start and stop, `pauseFor` about 2 s,
   inline errors with Open Settings and one-tap retry.
2. Voice PUT, two-step: a mic on the Add form's value field through the same
   `_listen` seam, transcript editable, Save stays a press, `pauseFor` 3 s or
   more with a stop control. Then tags as chips: recent tags plus a dictatable
   field, split through `_normKeys`. Values stay speakable text; URLs and
   passwords are typed.
3. iOS App Intents: Add-a-note and a recall intent in one pass, Swift calling
   the engine directly, confirmation dialog naming what was saved.
4. Android: the `ais://capture` deep link, a static shortcut, and the
   share-sheet receiver on the same route.
5. Later: the home-screen widget. Skip App Actions; adopt AppFunctions when it
   leaves preview.
