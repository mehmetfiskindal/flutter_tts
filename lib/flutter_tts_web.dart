import 'dart:async';
import 'dart:collection';
import 'dart:js_interop';
import 'dart:js_interop_unsafe';

import 'package:flutter/services.dart';
import 'package:flutter_web_plugins/flutter_web_plugins.dart';

import 'interop_types.dart';

enum TtsState { playing, stopped, paused, continued }

class FlutterTtsPlugin {
  static const String platformChannel = "flutter_tts";
  static late MethodChannel channel;
  bool awaitSpeakCompletion = false;

  TtsState ttsState = TtsState.stopped;

  Completer<dynamic>? _speechCompleter;

  bool get isPlaying => ttsState == TtsState.playing;

  bool get isStopped => ttsState == TtsState.stopped;

  bool get isPaused => ttsState == TtsState.paused;

  bool get isContinued => ttsState == TtsState.continued;

  static void registerWith(Registrar registrar) {
    channel =
        MethodChannel(platformChannel, const StandardMethodCodec(), registrar);
    final instance = FlutterTtsPlugin();
    channel.setMethodCallHandler(instance.handleMethodCall);
  }

  late final SpeechSynthesisUtterance utterance;
  List<SpeechSynthesisVoice> voices = [];
  List<String> languages = [];
  Timer? t;
  bool supported = false;
  Completer<void>? _voicesCompleter;

  FlutterTtsPlugin() {
    try {
      utterance = SpeechSynthesisUtterance();
      _listeners();
      synth.onVoicesChanged = ((JSAny? _) {
        _refreshVoices();
      }).toJS;
      _refreshVoices();
      supported = true;
    } catch (e) {
      print('Initialization of TTS failed. Functions are disabled. Error: $e');
    }
  }

  void _refreshVoices() {
    voices = synth.getVoices().toDart;
    languages = voices.map((voice) => voice.lang).toSet().toList();
    final completer = _voicesCompleter;
    if (completer != null && !completer.isCompleted && voices.isNotEmpty) {
      completer.complete();
    }
  }

  /// Web browsers load the voice list asynchronously. Wait for the
  /// `voiceschanged` event (with a timeout) instead of returning an empty list.
  Future<List<SpeechSynthesisVoice>> _ensureVoices() async {
    var current = synth.getVoices().toDart;
    if (current.isEmpty) {
      final completer = _voicesCompleter ??= Completer<void>();
      try {
        await completer.future.timeout(const Duration(seconds: 2));
      } on TimeoutException {
        // Some browsers never fire voiceschanged. Fall through and return
        // whatever is available.
      }
      if (identical(_voicesCompleter, completer)) {
        _voicesCompleter = null;
      }
      current = synth.getVoices().toDart;
    }

    if (current.isNotEmpty) {
      voices = current;
      languages = current.map((voice) => voice.lang).toSet().toList();
    }
    return current;
  }

  void _listeners() {
    utterance.onStart = (JSAny e) {
      ttsState = TtsState.playing;
      channel.invokeMethod("speak.onStart", null);
      var bLocal = (utterance.voice?.isLocalService ?? false);
      if (!bLocal) {
        t = Timer.periodic(Duration(seconds: 14), (t) {
          if (ttsState == TtsState.playing) {
            synth.pause();
            synth.resume();
          } else {
            t.cancel();
          }
        });
      }
    }.toJS;
    // js.JsFunction.withThis((e) {
    //   ttsState = TtsState.playing;
    //   channel.invokeMethod("speak.onStart", null);
    // });
    utterance.onEnd = (JSAny e) {
      ttsState = TtsState.stopped;
      _completeSpeech(1);
      t?.cancel();
      channel.invokeMethod("speak.onComplete", null);
    }.toJS;

    utterance.onPause = (JSAny e) {
      ttsState = TtsState.paused;
      channel.invokeMethod("speak.onPause", null);
    }.toJS;

    utterance.onResume = (JSAny e) {
      ttsState = TtsState.continued;
      channel.invokeMethod("speak.onContinue", null);
    }.toJS;

    utterance.onError = (JSObject event) {
      ttsState = TtsState.stopped;
      _completeSpeech(0);
      t?.cancel();
      print(event); // Log the entire event object to get more details
      channel.invokeMethod("speak.onError", event["error"]);
    }.toJS;

    utterance.onBoundary = (JSObject event) {
      final data = event.dartify();
      final eventMap = data is Map ? data : const <Object?, Object?>{};
      final charIndexValue = eventMap['charIndex'];
      final charIndex = charIndexValue is num ? charIndexValue.toInt() : 0;
      final name = eventMap['name']?.toString() ?? '';
      if (name == 'sentence') return;
      final text = utterance.text;
      if (charIndex < 0 || charIndex > text.length) return;
      var endIndex = charIndex;
      while (endIndex < text.length &&
          !RegExp(r'[\s,.!?]').hasMatch(text[endIndex])) {
        endIndex++;
      }
      String word = text.substring(charIndex, endIndex);
      Map<String, dynamic> progressArgs = {
        'text': text,
        'start': charIndex,
        'end': endIndex,
        'word': word
      };
      channel.invokeMethod("speak.onProgress", progressArgs);
    }.toJS;
  }

  Future<dynamic> handleMethodCall(MethodCall call) async {
    if (!supported) {
      switch (call.method) {
        case 'getLanguages':
          return <String>[];
        case 'getVoices':
          return <Map<String, String>>[];
        case 'isLanguageAvailable':
          return false;
        default:
          return 0;
      }
    }

    switch (call.method) {
      case 'speak':
        final text = call.arguments as String?;
        if (awaitSpeakCompletion) {
          _speechCompleter = Completer<dynamic>();
        }
        final started = _speak(text);
        if (started == 0) {
          _completeSpeech(0);
          return 0;
        }
        if (awaitSpeakCompletion) {
          return _speechCompleter?.future;
        }
        return 1;
      case 'awaitSpeakCompletion':
        awaitSpeakCompletion = (call.arguments as bool?) ?? false;
        return 1;
      case 'stop':
        _stop();
        return 1;
      case 'pause':
        _pause();
        return 1;
      case 'setLanguage':
        final language = call.arguments as String;
        return await _setLanguage(language) ? 1 : 0;
      case 'getLanguages':
        return await _getLanguages();
      case 'getVoices':
        return await getVoices();
      case 'setVoice':
        final tmpVoiceMap =
            Map<String, String>.from(call.arguments as LinkedHashMap);
        return await _setVoice(tmpVoiceMap);
      case 'setSpeechRate':
        final rate = call.arguments as double;
        _setRate(rate);
        return 1;
      case 'setVolume':
        final volume = call.arguments as double;
        _setVolume(volume);
        return 1;
      case 'setPitch':
        final pitch = call.arguments as double;
        _setPitch(pitch);
        return 1;
      case 'isLanguageAvailable':
        final lang = call.arguments as String;
        return await _isLanguageAvailable(lang);
      default:
        throw PlatformException(
            code: 'Unimplemented',
            details: "The flutter_tts plugin for web doesn't implement "
                "the method '${call.method}'");
    }
  }

  void _completeSpeech(dynamic value) {
    final completer = _speechCompleter;
    _speechCompleter = null;
    if (completer != null && !completer.isCompleted) {
      completer.complete(value);
    }
  }

  int _speak(String? text) {
    if (text == null || text.isEmpty) return 0;
    if (ttsState == TtsState.playing || ttsState == TtsState.continued) {
      return 0;
    }

    if (ttsState == TtsState.paused) {
      if (utterance.text == text) {
        synth.resume();
        return 1;
      }
      synth.cancel();
      ttsState = TtsState.stopped;
    }

    utterance.text = text;
    ttsState = TtsState.playing;
    synth.speak(utterance);
    return 1;
  }

  void _stop() {
    synth.cancel();
    ttsState = TtsState.stopped;
    t?.cancel();
    _completeSpeech(0);
  }

  void _pause() {
    if (ttsState == TtsState.playing || ttsState == TtsState.continued) {
      synth.pause();
    }
  }

  void _setRate(double rate) => utterance.rate = rate;
  void _setVolume(double volume) => utterance.volume = volume;
  void _setPitch(double pitch) => utterance.pitch = pitch;

  Future<bool> _setLanguage(String language) async {
    final normalized = language.trim().replaceAll('_', '-').toLowerCase();
    if (normalized.isEmpty) return false;

    final allVoices = await _ensureVoices();
    SpeechSynthesisVoice? match;
    for (final voice in allVoices) {
      final voiceLocale = voice.lang.replaceAll('_', '-').toLowerCase();
      if (voiceLocale == normalized || voiceLocale.startsWith('$normalized-')) {
        match = voice;
        break;
      }
    }

    if (match != null) {
      utterance.voice = match;
      utterance.lang = match.lang;
      return true;
    }

    // No installed voice for the exact language. Keep the requested language
    // so the browser can use its own fallback instead of silently continuing
    // with the previous language.
    utterance.voice = null;
    utterance.lang = normalized;
    return false;
  }

  Future<int> _setVoice(Map<String?, String?> voice) async {
    final name = voice['name'];
    final locale = voice['locale']?.replaceAll('_', '-').toLowerCase();
    if (name == null || locale == null) return 0;

    final allVoices = await _ensureVoices();
    for (final candidate in allVoices) {
      final candidateLocale = candidate.lang.replaceAll('_', '-').toLowerCase();
      if (candidate.name == name && candidateLocale == locale) {
        utterance.voice = candidate;
        utterance.lang = candidate.lang;
        return 1;
      }
    }
    return 0;
  }

  Future<bool> _isLanguageAvailable(String? language) async {
    if (language == null || language.trim().isEmpty) return false;
    final normalized = language.trim().replaceAll('_', '-').toLowerCase();

    final allVoices = await _ensureVoices();
    return allVoices.any((voice) {
      final voiceLocale = voice.lang.replaceAll('_', '-').toLowerCase();
      return voiceLocale == normalized ||
          (!normalized.contains('-') &&
              voiceLocale.split('-').first == normalized);
    });
  }

  Future<List<String>> _getLanguages() async {
    await _ensureVoices();
    return languages;
  }

  Future<List<Map<String, String>>> getVoices() async {
    final allVoices = await _ensureVoices();
    return allVoices
        .map((voice) => {"name": voice.name, "locale": voice.lang})
        .toList();
  }
}
