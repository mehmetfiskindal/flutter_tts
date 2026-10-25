import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_tts/flutter_tts.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  const channel = MethodChannel('flutter_tts');
  late FlutterTts tts;
  late List<MethodCall> calls;

  final voices = <Map<String, dynamic>>[
    {'name': 'Arabic SA', 'locale': 'ar-SA'},
    {'name': 'Arabic EG', 'locale': 'ar-EG'},
    {'name': 'English US', 'locale': 'en-US'},
    {'name': 'English GB', 'locale': 'en-GB'},
    {'name': 'French FR', 'locale': 'fr-FR'},
    {'name': 'No locale'},
  ];

  setUp(() {
    calls = <MethodCall>[];
    tts = FlutterTts();

    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
      calls.add(call);
      switch (call.method) {
        case 'getVoices':
          return voices;
        case 'getLanguages':
          return <String>['ar-SA', 'en-US', 'fr-FR'];
        case 'getEngines':
          return <String>['com.google.android.tts'];
        case 'getDefaultEngine':
          return 'com.google.android.tts';
        case 'getDefaultVoice':
          return <String, String>{'name': 'English US', 'locale': 'en-US'};
        case 'isLanguageAvailable':
          return call.arguments == 'ar';
        case 'isLanguageInstalled':
          return call.arguments == 'ar-SA';
        case 'areLanguagesInstalled':
          return <String, bool>{'ar-SA': true, 'xx-XX': false};
        case 'getSpeechRateValidRange':
          return <String, String>{
            'min': '0.0',
            'normal': '0.5',
            'max': '1.0',
            'platform': 'macos',
          };
        case 'awaitSpeakCompletion':
        case 'awaitSynthCompletion':
        case 'setLanguage':
        case 'setSpeechRate':
        case 'setVolume':
        case 'setPitch':
        case 'setSilence':
        case 'setQueueMode':
        case 'speak':
        case 'stop':
        case 'pause':
          return 1;
      }
      return null;
    });
  });

  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, null);
  });

  group('filterVoicesByLocale', () {
    test('matches every regional variant for a bare language code', () {
      final result = filterVoicesByLocale(voices, 'ar');

      expect(result.map((voice) => voice['name']), ['Arabic SA', 'Arabic EG']);
    });

    test('matches the exact locale when a region is provided', () {
      final result = filterVoicesByLocale(voices, 'en-gb');

      expect(result.map((voice) => voice['name']), ['English GB']);
    });

    test('can ignore the region explicitly', () {
      final result =
          filterVoicesByLocale(voices, 'en-GB', matchLanguageOnly: true);

      expect(
          result.map((voice) => voice['name']), ['English US', 'English GB']);
    });

    test('normalizes underscores and casing', () {
      final result = filterVoicesByLocale(voices, 'FR_fr');

      expect(result.map((voice) => voice['name']), ['French FR']);
    });

    test('ignores entries without a locale', () {
      final result = filterVoicesByLocale(voices, 'xx');

      expect(result, isEmpty);
    });
  });

  group('FlutterTts typed getters', () {
    test('getVoicesForLanguage filters through the platform channel', () async {
      final result = await tts.getVoicesForLanguage('ar');

      expect(result.map((voice) => voice['name']), ['Arabic SA', 'Arabic EG']);
      expect(calls.single.method, 'getVoices');
    });

    test('getVoicesForLocale can filter a regional locale', () async {
      final result = await tts.getVoicesForLocale('en-GB');

      expect(result.map((voice) => voice['name']), ['English GB']);
    });

    test('getLanguages returns a typed list', () async {
      expect(await tts.getLanguages, <String>['ar-SA', 'en-US', 'fr-FR']);
    });

    test('getEngines returns a typed list', () async {
      expect(await tts.getEngines, <String>['com.google.android.tts']);
    });

    test('getDefaultEngine returns a nullable string', () async {
      expect(await tts.getDefaultEngine, 'com.google.android.tts');
    });

    test('getDefaultVoice returns a typed map', () async {
      expect(await tts.getDefaultVoice, <String, dynamic>{
        'name': 'English US',
        'locale': 'en-US',
      });
    });

    test('isLanguageAvailable returns a bool', () async {
      expect(await tts.isLanguageAvailable('ar'), isTrue);
      expect(await tts.isLanguageAvailable('xx'), isFalse);
    });

    test('isLanguageInstalled returns a bool', () async {
      expect(await tts.isLanguageInstalled('ar-SA'), isTrue);
      expect(await tts.isLanguageInstalled('xx-XX'), isFalse);
    });

    test('areLanguagesInstalled returns a typed map', () async {
      expect(await tts.areLanguagesInstalled(['ar-SA', 'xx-XX']),
          <String, bool>{'ar-SA': true, 'xx-XX': false});
    });

    test('getSpeechRateValidRange understands the macOS platform', () async {
      final range = await tts.getSpeechRateValidRange;

      expect(range.platform, TextToSpeechPlatform.macos);
      expect(range.min, 0.0);
      expect(range.normal, 0.5);
      expect(range.max, 1.0);
    });
  });
}
