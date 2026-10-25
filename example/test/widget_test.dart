import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:flutter_tts_example/main.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('renders the TTS demo screen', (WidgetTester tester) async {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(const MethodChannel('flutter_tts'),
            (MethodCall call) async {
      switch (call.method) {
        case 'getVoices':
          return <Map<String, String>>[
            <String, String>{'name': 'Test Voice', 'locale': 'en-US'},
          ];
        case 'getLanguages':
          return <String>['en-US'];
        case 'getEngines':
          return <String>[];
        case 'getDefaultEngine':
          return null;
        case 'getDefaultVoice':
          return <String, String>{};
        default:
          return 1;
      }
    });

    await tester.pumpWidget(const MyApp());
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 100));

    expect(find.text('Flutter TTS'), findsOneWidget);
  });
}
