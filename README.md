# Text To Speech

[![pub package](https://img.shields.io/pub/v/flutter_tts.svg?style=for-the-badge&colorB=green)](https://pub.dartlang.org/packages/flutter_tts)

A flutter text to speech plugin (Swift,Kotlin)

## Features

- [x] Android, iOS, Web, Windows & macOS
  - [x] speak
  - [x] stop
  - [x] get languages
  - [x] set language
  - [x] set speech rate
  - [x] set speech volume
  - [x] set speech pitch
  - [x] get voices
  - [x] set voice
- [x] Android, iOS, Web & macOS
  - [x] is language available
- [x] Android, iOS, Web
  - [x] speech marks (requires iOS 7+, Android 26+, and default voice engine for web)
- [x] Android, iOS, macOS
  - [x] synthesize to file (requires iOS 13+ / macOS 10.15+)
- [x] Android, iOS, macOS, Web, & Windows
  - [x] pause
- [x] Android
  - [x] set silence
  - [x] is language installed
  - [x] are languages installed
  - [x] get engines
  - [x] set engine
  - [x] get default engine
  - [x] get default voice
  - [x] set queue mode
  - [x] get max speech input length
- [x] iOS
  - [x] set shared instance
  - [x] set audio session category

## Usage

## macOS

```bash
macOS version: 10.15 or newer
```

[Example App](https://github.com/dlutton/flutter_tts/tree/macOS_app) from the macOS_app branch

## Web

[Website](https://dlutton.github.io/flutter_tts) from the example directory.

**Progress updates on Web**

Progress updates are only supported for native speech synsthesis. Use the default engine to ensure support for progress updates. [Chromium#41195426](https://issues.chromium.org/issues/41195426#comment8)

## Android

Use a minimum Android SDK version of 24 (or higher) in your `android/app/build.gradle` file. This is the minimum supported by the plugin.

```groovy
minSdkVersion 24
```

**Kotlin and Android Gradle Plugin**

The plugin is built with Kotlin 2.2 and Android Gradle Plugin 8.13. Use a compatible Android Gradle Plugin and Gradle version; current Flutter stable templates already satisfy these requirements. If your project was created with an older Flutter version, update the Kotlin plugin in `android/settings.gradle`:

```groovy
id "org.jetbrains.kotlin.android" version "2.2.20" apply false
```

Version 4.3.0 of the plugin declares
[`TextToSpeech.Engine.INTENT_ACTION_TTS_SERVICE`](https://developer.android.com/reference/android/speech/tts/TextToSpeech.Engine#INTENT_ACTION_TTS_SERVICE)
in its own manifest. Host apps using 4.3.0 or newer do not need to add the
`queries` element themselves. Apps that target Android 11+ and use an older
plugin version should add it:

```xml
<queries>
  <intent>
    <action android:name="android.intent.action.TTS_SERVICE" />
  </intent>
</queries>
```

### Pausing on Android

Android TTS does not support the pause function natively, so we have implemented a work around. We utilize the native `onRangeStart()` method to determine the index of start when `pause` is invoked. We use that index to create a new text the next time `speak` is invoked.  Due to using `onRangeStart()`, pause works on SDK versions >= 26.  Also, if using `start` and `end` offsets inside of `setProgressHandler()`, you'll need to keep a track of them if using `pause` since they will update once the new text is created when `speak` is called after being paused.

```dart
await flutterTts.pause()
```

## iOS

There's a known issue with integrating plugins that use Swift into a Flutter project created with the Objective-C template. [Flutter#16049](https://github.com/flutter/flutter/issues/16049)

[Example](https://github.com/dlutton/flutter_tts/blob/master/example/lib/main.dart)

To use this plugin :

- add the dependency to your [pubspec.yaml](https://github.com/dlutton/flutter_tts/blob/master/example/pubspec.yaml) file.

```yaml
  dependencies:
    flutter:
      sdk: flutter
    flutter_tts:
```

- instantiate FlutterTts

```dart
FlutterTts flutterTts = FlutterTts();

```

To set shared audio [instance](https://developer.apple.com/documentation/avfoundation/avaudiosession/1616504-sharedinstance) (iOS only):

```dart
await flutterTts.setSharedInstance(true);
```

To set audio [category and options](https://developer.apple.com/documentation/avfoundation/avaudiosession) with optional [mode](https://developer.apple.com/documentation/avfaudio/avaudiosession/mode) (iOS only). The following setup allows background music and in-app audio session to continue simultaneously:

```dart
await flutterTts.setIosAudioCategory(IosTextToSpeechAudioCategory.ambient,
     [
          IosTextToSpeechAudioCategoryOptions.allowBluetooth,
          IosTextToSpeechAudioCategoryOptions.allowBluetoothA2DP,
          IosTextToSpeechAudioCategoryOptions.mixWithOthers
     ],
     IosTextToSpeechAudioMode.voicePrompt
);
```

To await speak completion.

```dart
await flutterTts.awaitSpeakCompletion(true);
```

To await synthesize to file completion.

```dart
await flutterTts.awaitSynthCompletion(true);
```

### speak, stop, getLanguages, setLanguage, setSpeechRate, getVoices, setVoice, setVolume, setPitch, isLanguageAvailable, setSharedInstance

```dart
Future _speak() async{
    var result = await flutterTts.speak("Hello World");
    if (result == 1) setState(() => ttsState = TtsState.playing);
}

Future _stop() async{
    var result = await flutterTts.stop();
    if (result == 1) setState(() => ttsState = TtsState.stopped);
}

List<String> languages = await flutterTts.getLanguages;

await flutterTts.setLanguage("en-US");

await flutterTts.setSpeechRate(1.0);

await flutterTts.setVolume(1.0);

await flutterTts.setPitch(1.0);

await flutterTts.isLanguageAvailable("en-US");

// Android, iOS, macOS, Web and Windows
//see the "Pausing on Android" section for more info
await flutterTts.pause();

// iOS, macOS, and Android only
// The last parameter is an optional boolean value for isFullPath (defaults to false)
await flutterTts.synthesizeToFile("Hello World", Platform.isAndroid ? "tts.wav" : "tts.caf", false);

// Each voice is a Map containing at least these keys: name, locale
// - Windows (UWP and SAPI voices): gender, identifier
// - iOS, macOS only: quality, gender, identifier
// - Android only: quality, latency, network_required, features
List<Map<String, dynamic>> voices = await flutterTts.getVoices;

// Select the voices for one language. A bare language code matches every
// regional variant, so this returns ar-SA, ar-EG, ar-AE, ...
List<Map<String, dynamic>> arabicVoices =
    await flutterTts.getVoicesForLanguage('ar');

// Use a full locale when you need an exact match.
List<Map<String, dynamic>> britishVoices =
    await flutterTts.getVoicesForLocale('en-GB');

// The same filtering is available as a pure helper, so an existing list can
// be filtered without another platform call. Chinese, French, ...
List<Map<String, dynamic>> chineseVoices =
    filterVoicesByLocale(voices, 'zh');

// Keys are also available as constants for type-safe access.
for (final voice in arabicVoices) {
  print('${voice[TtsVoiceKeys.name]} (${voice[TtsVoiceKeys.locale]})');
}

await flutterTts.setVoice({"name": "Karen", "locale": "en-AU"});
// iOS, macOS only
await flutterTts.setVoice({"identifier": "com.apple.voice.compact.en-AU.Karen"});

// iOS only
await flutterTts.setSharedInstance(true);

// Android only
await flutterTts.speak("Hello World", focus: true);

await flutterTts.setSilence(2);

await flutterTts.getEngines;

await flutterTts.getDefaultVoice;

await flutterTts.isLanguageInstalled("en-AU");

await flutterTts.areLanguagesInstalled(["en-AU", "en-US"]);

await flutterTts.setQueueMode(1);

await flutterTts.getMaxSpeechInputLength;

await flutterTts.setAudioAttributesForNavigation();
```

### Getting voices for a specific language

`getVoicesForLanguage` filters the installed voices by language, while
`getVoicesForLocale` filters by a specific locale. Both accept BCP-47 tags.
A bare language code such as `ar`, `zh`, or `fr` returns every regional
variant installed on the device. If you already have the list from
`getVoices`, use the pure `filterVoicesByLocale` helper instead.

```dart
final arabic = await flutterTts.getVoicesForLanguage('ar');
final french = await flutterTts.getVoicesForLanguage('fr');
final chinese = await flutterTts.getVoicesForLanguage('zh');
```

### Listening for platform calls

```dart
flutterTts.setStartHandler(() {
  setState(() {
    ttsState = TtsState.playing;
  });
});

flutterTts.setCompletionHandler(() {
  setState(() {
    ttsState = TtsState.stopped;
  });
});

flutterTts.setProgressHandler((String text, int startOffset, int endOffset, String word) {
  setState(() {
    _currentWord = word;
  });
});

flutterTts.setErrorHandler((msg) {
  setState(() {
    ttsState = TtsState.stopped;
  });
});

flutterTts.setCancelHandler((msg) {
  setState(() {
    ttsState = TtsState.stopped;
  });
});

// Android, iOS and Web
flutterTts.setPauseHandler((msg) {
  setState(() {
    ttsState = TtsState.paused;
  });
});

flutterTts.setContinueHandler((msg) {
  setState(() {
    ttsState = TtsState.continued;
  });
});

```

## Getting Started

For help getting started with Flutter, view our online
[documentation](https://flutter.dev/).

For help on editing plugin code, view the [documentation](https://flutter.dev/platform-plugins/#edit-code).
