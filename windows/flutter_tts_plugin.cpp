#include "include/flutter_tts/flutter_tts_plugin.h"
// This must be included before many other Windows headers.
#include <windows.h>
#include <ppltasks.h>
#include <VersionHelpers.h>
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

typedef std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> FlutterResult;
//typedef flutter::MethodResult<flutter::EncodableValue>* PFlutterResult;

// Custom window message used to run TTS completion callbacks on the platform
// thread. Posting to the Flutter view's HWND is used instead of calling the
// method channel directly from a worker thread (issue #629).
constexpr UINT kTtsEventMessage = WM_APP + 0x7F01;

std::unique_ptr<flutter::MethodChannel<>> methodChannel;

namespace {
	const flutter::EncodableValue* GetArgument(
		const flutter::MethodCall<flutter::EncodableValue>& call,
		size_t index) {
		const auto* arguments = call.arguments();
		if (arguments == nullptr) {
			return nullptr;
		}
		// StandardMethodCodec encodes a single Dart argument as the value
		// itself. Some callers pass a list, so support both shapes.
		if (const auto* list =
			std::get_if<flutter::EncodableList>(arguments)) {
			if (list->size() <= index) {
				return nullptr;
			}
			return &(*list)[index];
		}
		return index == 0 ? arguments : nullptr;
	}
}


#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP)
#include <winrt/Windows.Media.SpeechSynthesis.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.Media.Core.h>
using namespace winrt;
using namespace Windows::Media::SpeechSynthesis;
using namespace Concurrency;
using namespace std::chrono_literals;
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
namespace {
	class FlutterTtsPlugin : public flutter::Plugin {
	public:
		static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);
		FlutterTtsPlugin();
		virtual ~FlutterTtsPlugin();
	private:
		// Called when a method is called on this plugin's channel from Dart.
		void HandleMethodCall(
			const flutter::MethodCall<flutter::EncodableValue>& method_call,
			std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
		void speak(const std::string, FlutterResult);
		void pause();
		void continuePlay();
		void stop();
		void setVolume(const double);
		void setPitch(const double);
		void setRate(const double);
		void getVoices(flutter::EncodableList&);
		void setVoice(const std::string, const std::string, FlutterResult&);
		void getLanguages(flutter::EncodableList&);
		void setLanguage(const std::string, FlutterResult&);
		void addMplayer();
		winrt::Windows::Foundation::IAsyncAction asyncSpeak(const std::string);
		bool speaking();
		bool paused();
		SpeechSynthesizer synth;
		winrt::Windows::Media::Playback::MediaPlayer mPlayer;
		std::atomic<bool> isPaused{ false };
		std::atomic<bool> isSpeaking{ false };
		std::atomic<bool> awaitSpeakCompletion{ false };
		FlutterResult speakResult;
		std::mutex resultMutex;
		flutter::PluginRegistrarWindows* registrar_ = nullptr;
		HWND window_handle_ = nullptr;
		int window_proc_id_ = -1;
		void completeSpeakResult(int value);
		void postEvent(int event);
		void handleEvent(int event);
	};

	void FlutterTtsPlugin::RegisterWithRegistrar(
		flutter::PluginRegistrarWindows* registrar) {
		methodChannel =
			std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
				registrar->messenger(), "flutter_tts",
				&flutter::StandardMethodCodec::GetInstance());
		auto plugin = std::make_unique<FlutterTtsPlugin>();
		plugin->registrar_ = registrar;
		if (auto* view = registrar->GetView()) {
			plugin->window_handle_ = GetAncestor(view->GetNativeWindow(), GA_ROOT);
		}
		plugin->window_proc_id_ = registrar->RegisterTopLevelWindowProcDelegate(
			[plugin_pointer = plugin.get()](HWND hwnd, UINT message,
				WPARAM wparam, LPARAM lparam) -> std::optional<LRESULT> {
				if (message == kTtsEventMessage) {
					plugin_pointer->handleEvent(static_cast<int>(wparam));
					return 0;
				}
				return std::nullopt;
			});

		methodChannel->SetMethodCallHandler(
			[plugin_pointer = plugin.get()](const auto& call, auto result) {
			plugin_pointer->HandleMethodCall(call, std::move(result));
		});
		registrar->AddPlugin(std::move(plugin));
	}

	void FlutterTtsPlugin::addMplayer() {
		mPlayer = winrt::Windows::Media::Playback::MediaPlayer();
		mPlayer.MediaEnded([this](Windows::Media::Playback::MediaPlayer const& sender,
			Windows::Foundation::IInspectable const& args)
			{
				isSpeaking = false;
				postEvent(1);
			});
	}

	void FlutterTtsPlugin::completeSpeakResult(int value) {
		FlutterResult pending;
		{
			std::lock_guard<std::mutex> lock(resultMutex);
			pending = std::move(speakResult);
		}
		if (pending) {
			pending->Success(value);
		}
	}

	void FlutterTtsPlugin::postEvent(int event) {
		if (window_handle_ != nullptr &&
			PostMessage(window_handle_, kTtsEventMessage,
				static_cast<WPARAM>(event), 0)) {
			return;
		}
		// The view is unavailable (for example during shutdown), so fall back to
		// handling the event directly rather than leaving a future pending.
		handleEvent(event);
	}

	void FlutterTtsPlugin::handleEvent(int event) {
		switch (event) {
		case 1:
			methodChannel->InvokeMethod("speak.onComplete", NULL);
			completeSpeakResult(1);
			break;
		case 2:
			methodChannel->InvokeMethod("speak.onError", NULL);
			completeSpeakResult(0);
			break;
		case 3:
			methodChannel->InvokeMethod("speak.onCancel", NULL);
			break;
		default:
			break;
		}
	}

	bool FlutterTtsPlugin::speaking() {
		return isSpeaking;
	}

	bool FlutterTtsPlugin::paused() {
		return isPaused;
	}

	winrt::Windows::Foundation::IAsyncAction FlutterTtsPlugin::asyncSpeak(const std::string text) {
		SpeechSynthesisStream speechStream{
		  co_await synth.SynthesizeTextToStreamAsync(to_hstring(text))
		};
		winrt::param::hstring cType = L"Audio";
		winrt::Windows::Media::Core::MediaSource source =
			winrt::Windows::Media::Core::MediaSource::CreateFromStream(speechStream, cType);
		mPlayer.Source(source);
		mPlayer.Play();
	}

	void FlutterTtsPlugin::speak(const std::string text, FlutterResult result) {
		if (text.empty()) {
			result->Success(0);
			return;
		}

		isSpeaking = true;
		isPaused = false;
		if (awaitSpeakCompletion) {
			std::lock_guard<std::mutex> lock(resultMutex);
			speakResult = std::move(result);
		} else {
			result->Success(1);
		}

		try {
			auto my_task{ asyncSpeak(text) };
			my_task.Completed([this](const auto&, winrt::Windows::Foundation::AsyncStatus status) {
				if (status != winrt::Windows::Foundation::AsyncStatus::Completed) {
					isSpeaking = false;
					postEvent(2);
				}
			});
			methodChannel->InvokeMethod("speak.onStart", NULL);
		} catch (const winrt::hresult_error& error) {
			isSpeaking = false;
			postEvent(2);
			OutputDebugStringW(error.message().c_str());
		}
	};

	void FlutterTtsPlugin::pause() {
		mPlayer.Pause();
		isPaused = true;
		methodChannel->InvokeMethod("speak.onPause", NULL);
	}

	void FlutterTtsPlugin::continuePlay() {
		mPlayer.Play();
		isPaused = false;
		methodChannel->InvokeMethod("speak.onContinue", NULL);
	}

	void FlutterTtsPlugin::stop() {
	    methodChannel->InvokeMethod("speak.onCancel", NULL);
		completeSpeakResult(0);

		mPlayer.Close();
		addMplayer();
		isSpeaking = false;
		isPaused = false;
	}
	void FlutterTtsPlugin::setVolume(const double newVolume) { synth.Options().AudioVolume(newVolume); }

	void FlutterTtsPlugin::setPitch(const double newPitch) { synth.Options().AudioPitch(newPitch); }

	void FlutterTtsPlugin::setRate(const double newRate) { synth.Options().SpeakingRate(newRate + 0.5); }

	void FlutterTtsPlugin::getVoices(flutter::EncodableList& voices) {
		auto synthVoices = synth.AllVoices();
		std::for_each(begin(synthVoices), end(synthVoices), [&voices](const VoiceInformation& voice)
			{
				flutter::EncodableMap voiceInfo;
				voiceInfo[flutter::EncodableValue("locale")] = to_string(voice.Language());
				voiceInfo[flutter::EncodableValue("name")] = to_string(voice.DisplayName());
				//  Convert VoiceGender to string
				std::string gender;
				switch (voice.Gender()) {
					case VoiceGender::Male:
						gender = "male";
						break;
					case VoiceGender::Female:
						gender = "female";
						break;
					default:
						gender = "unknown";
						break;
				}
				voiceInfo[flutter::EncodableValue("gender")] = gender; 
				// Identifier example "HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech_OneCore\Voices\Tokens\MSTTS_V110_enUS_MarkM"
				voiceInfo[flutter::EncodableValue("identifier")] = to_string(voice.Id());
				voices.push_back(flutter::EncodableMap(voiceInfo));
			});
	}

	void FlutterTtsPlugin::setVoice(const std::string voiceLanguage, const std::string voiceName, FlutterResult& result) {
		bool found = false;
		auto voices = synth.AllVoices();
		VoiceInformation newVoice = synth.Voice();
		std::for_each(begin(voices), end(voices), [&voiceLanguage, &voiceName, &found, &newVoice](const VoiceInformation& voice)
			{
				if (!found && to_string(voice.Language()) == voiceLanguage && to_string(voice.DisplayName()) == voiceName)
				{
					newVoice = voice;
					found = true;
				}
			});
		if (found) {
			synth.Voice(newVoice);
			result->Success(1);
		} else {
			result->Success(0);
		}
	}

	void FlutterTtsPlugin::getLanguages(flutter::EncodableList& languages) {
		auto synthVoices = synth.AllVoices();
		std::set<flutter::EncodableValue> languagesSet = {};
		std::for_each(begin(synthVoices), end(synthVoices), [&languagesSet](const VoiceInformation& voice)
			{
				languagesSet.insert(flutter::EncodableValue(to_string(voice.Language())));
			});
		std::for_each(begin(languagesSet), end(languagesSet), [&languages](const flutter::EncodableValue value)
			{
				languages.push_back(value);
			});
	}
	void FlutterTtsPlugin::setLanguage(const std::string voiceLanguage, FlutterResult& result) {
		bool found = false;
		auto voices = synth.AllVoices();
		VoiceInformation newVoice = synth.Voice();
		std::for_each(begin(voices), end(voices), [&voiceLanguage, &newVoice, &found](const VoiceInformation& voice)
			{
				const std::string language = to_string(voice.Language());
				const bool matches = language == voiceLanguage ||
					(voiceLanguage.find('-') == std::string::npos &&
					 language.rfind(voiceLanguage + "-", 0) == 0);
				if (!found && matches) {
					newVoice = voice;
					found = true;
				}
			});
		if (found) {
			synth.Voice(newVoice);
			result->Success(1);
		} else {
			result->Success(0);
		}
	}


	FlutterTtsPlugin::FlutterTtsPlugin() {
		synth = SpeechSynthesizer();
		addMplayer();
		isPaused = false;
		isSpeaking = false;
		awaitSpeakCompletion = false;
		speakResult = FlutterResult();
	}

	FlutterTtsPlugin::~FlutterTtsPlugin() {
		if (registrar_ != nullptr && window_proc_id_ >= 0) {
			registrar_->UnregisterTopLevelWindowProcDelegate(window_proc_id_);
			window_proc_id_ = -1;
		}
		completeSpeakResult(0);
		mPlayer.Close();
	}

	void FlutterTtsPlugin::HandleMethodCall(
		const flutter::MethodCall<flutter::EncodableValue>& method_call,
		FlutterResult result) {
		if (method_call.method_name().compare("getPlatformVersion") == 0) {
			std::ostringstream version_stream;
			version_stream << "Windows UWP";
			result->Success(flutter::EncodableValue(version_stream.str()));
			return;
		}

#else
#include <string>
#include <atlbase.h>
#include <atlstr.h>
#include <array>
#include <sapi.h>
#pragma warning(disable:4996)
#include <sphelper.h>
#pragma warning(default: 4996)
namespace {

	bool GetTokenAttribute(ISpObjectToken* token, LPCWSTR key, std::string& value) {
		CComPtr<ISpDataKey> attributes;
		if (token == nullptr || FAILED(token->OpenKey(L"Attributes", &attributes)) || !attributes) {
			return false;
		}
		WCHAR* rawValue = nullptr;
		if (FAILED(attributes->GetStringValue(key, &rawValue)) || rawValue == nullptr) {
			return false;
		}
		value = CW2A(rawValue);
		::CoTaskMemFree(rawValue);
		return true;
	}

	bool GetTokenLanguage(ISpObjectToken* token, std::string& language) {
		std::string hexLanguage;
		if (!GetTokenAttribute(token, L"Language", hexLanguage)) {
			return false;
		}
		wchar_t locale[25] = {};
		const LCID lcid = static_cast<LCID>(std::strtol(hexLanguage.c_str(), nullptr, 16));
		if (LCIDToLocaleName(lcid, locale, 25, 0) == 0) {
			return false;
		}
		language = CW2A(locale);
		return true;
	}

	bool GetTokenId(ISpObjectToken* token, std::string& id) {
		LPWSTR rawId = nullptr;
		if (token == nullptr || FAILED(token->GetId(&rawId)) || rawId == nullptr) {
			return false;
		}
		id = CW2A(rawId);
		::CoTaskMemFree(rawId);
		return true;
	}

	class FlutterTtsPlugin : public flutter::Plugin {
	public:
		static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);
		FlutterTtsPlugin();
		virtual ~FlutterTtsPlugin();
	private:
		// Called when a method is called on this plugin's channel from Dart.
		void HandleMethodCall(
			const flutter::MethodCall<flutter::EncodableValue>& method_call,
			std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

		void speak(const std::string, FlutterResult);
		void pause();
		void continuePlay();
		void stop();
		void setVolume(const double);
		void setPitch(const double);
		void setRate(const double);
		void getVoices(flutter::EncodableList&);
		void setVoice(const std::string, const std::string, FlutterResult&);
		void getLanguages(flutter::EncodableList&);
		void setLanguage(const std::string, FlutterResult&);

		ISpVoice* pVoice = nullptr;
		std::atomic<bool> awaitSpeakCompletion{ false };
		std::atomic<bool> isPaused{ false };
		double pitch = 0;
		bool speaking();
		bool paused();
		FlutterResult speakResult;
		std::mutex resultMutex;
		HANDLE addWaitHandle = nullptr;
		bool comInitialized = false;
		flutter::PluginRegistrarWindows* registrar_ = nullptr;
		HWND window_handle_ = nullptr;
		int window_proc_id_ = -1;
		void completeSpeakResult(int value);
		void postEvent(int event);
		void handleEvent(int event);
		static void CALLBACK WaitCallback(PVOID lpParam, BOOLEAN timerOrWaitFired);
	};

	void FlutterTtsPlugin::RegisterWithRegistrar(
		flutter::PluginRegistrarWindows* registrar) {
		methodChannel =
			std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
				registrar->messenger(), "flutter_tts",
				&flutter::StandardMethodCodec::GetInstance());
		auto plugin = std::make_unique<FlutterTtsPlugin>();
		plugin->registrar_ = registrar;
		if (auto* view = registrar->GetView()) {
			plugin->window_handle_ = GetAncestor(view->GetNativeWindow(), GA_ROOT);
		}
		plugin->window_proc_id_ = registrar->RegisterTopLevelWindowProcDelegate(
			[plugin_pointer = plugin.get()](HWND hwnd, UINT message,
				WPARAM wparam, LPARAM lparam) -> std::optional<LRESULT> {
				if (message == kTtsEventMessage) {
					plugin_pointer->handleEvent(static_cast<int>(wparam));
					return 0;
				}
				return std::nullopt;
			});
		methodChannel->SetMethodCallHandler(
			[plugin_pointer = plugin.get()](const auto& call, auto result) {
			plugin_pointer->HandleMethodCall(call, std::move(result));
		});

		registrar->AddPlugin(std::move(plugin));
	}

	FlutterTtsPlugin::FlutterTtsPlugin() {
		HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		if (SUCCEEDED(hr)) {
			comInitialized = true;
		} else if (hr != RPC_E_CHANGED_MODE) {
			throw std::runtime_error("TTS init failed");
		}

		hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&pVoice);
		if (FAILED(hr)) {
			if (comInitialized) {
				::CoUninitialize();
				comInitialized = false;
			}
			throw std::runtime_error("TTS create instance failed");
		}
	}

	FlutterTtsPlugin::~FlutterTtsPlugin() {
		if (registrar_ != nullptr && window_proc_id_ >= 0) {
			registrar_->UnregisterTopLevelWindowProcDelegate(window_proc_id_);
			window_proc_id_ = -1;
		}
		if (addWaitHandle != nullptr) {
			UnregisterWaitEx(addWaitHandle, INVALID_HANDLE_VALUE);
			addWaitHandle = nullptr;
		}
		if (pVoice != nullptr) {
			pVoice->Speak(L"", SPF_PURGEBEFORESPEAK, NULL);
			pVoice->Release();
			pVoice = nullptr;
		}
		if (comInitialized) {
			::CoUninitialize();
		}
	}

	void FlutterTtsPlugin::completeSpeakResult(int value) {
		FlutterResult pending;
		{
			std::lock_guard<std::mutex> lock(resultMutex);
			pending = std::move(speakResult);
		}
		if (pending) {
			pending->Success(value);
		}
	}

	void FlutterTtsPlugin::postEvent(int event) {
		if (window_handle_ != nullptr &&
			PostMessage(window_handle_, kTtsEventMessage,
				static_cast<WPARAM>(event), 0)) {
			return;
		}
		// The view is unavailable (for example during shutdown), so fall back to
		// handling the event directly rather than leaving a future pending.
		handleEvent(event);
	}

	void FlutterTtsPlugin::handleEvent(int event) {
		switch (event) {
		case 1:
			methodChannel->InvokeMethod("speak.onComplete", NULL);
			completeSpeakResult(1);
			break;
		case 2:
			methodChannel->InvokeMethod("speak.onError", NULL);
			completeSpeakResult(0);
			break;
		case 3:
			methodChannel->InvokeMethod("speak.onCancel", NULL);
			break;
		default:
			break;
		}
	}

	bool FlutterTtsPlugin::speaking()
	{
		if (pVoice == nullptr) return false;
		SPVOICESTATUS status;
		if (FAILED(pVoice->GetStatus(&status, NULL))) return false;
		return status.dwRunningState == SPRS_IS_SPEAKING;
	}
	bool FlutterTtsPlugin::paused() { return isPaused; }


	void FlutterTtsPlugin::speak(const std::string text, FlutterResult result) {
		if (pVoice == nullptr) {
			result->Error("TtsError", "Speech synthesizer is not available");
			return;
		}
		if (text.empty()) {
			result->Success(0);
			return;
		}

		const std::string arg = "<PITCH MIDDLE = '" + std::to_string(int((pitch - 1) * 10 * (1 + (pitch < 1)) )) + "'/>" + text;
		const int wchars_num = MultiByteToWideChar(CP_UTF8, 0, arg.c_str(), -1, NULL, 0);
		if (wchars_num <= 0) {
			result->Success(0);
			return;
		}
		std::wstring wstr(wchars_num, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, arg.c_str(), -1, wstr.data(), wchars_num);

		const bool awaiting = awaitSpeakCompletion;
		if (awaiting) {
			std::lock_guard<std::mutex> lock(resultMutex);
			speakResult = std::move(result);
		}

		const HRESULT hr = pVoice->Speak(wstr.c_str(), SPF_ASYNC, NULL);
		if (FAILED(hr)) {
			completeSpeakResult(0);
			if (!awaiting) result->Success(0);
			return;
		}

		methodChannel->InvokeMethod("speak.onStart", NULL);
		if (!awaiting) {
			result->Success(1);
			return;
		}

		if (addWaitHandle != nullptr) {
			UnregisterWaitEx(addWaitHandle, NULL);
			addWaitHandle = nullptr;
		}
		const HANDLE speakCompletionHandle = pVoice->SpeakCompleteEvent();
		if (!RegisterWaitForSingleObject(&addWaitHandle, speakCompletionHandle,
			FlutterTtsPlugin::WaitCallback, this, INFINITE, WT_EXECUTEONLYONCE)) {
			addWaitHandle = nullptr;
			completeSpeakResult(0);
		}
	}
	void CALLBACK FlutterTtsPlugin::WaitCallback(PVOID lpParam, BOOLEAN timerOrWaitFired) {
		auto* self = static_cast<FlutterTtsPlugin*>(lpParam);
		if (self != nullptr) {
			self->postEvent(1);
		}
	}
	void FlutterTtsPlugin::pause()
	{
		if (pVoice == nullptr) return;
		if (!isPaused)
		{
			pVoice->Pause();
			isPaused = true;
		}
	    methodChannel->InvokeMethod("speak.onPause", NULL);
	}
	void FlutterTtsPlugin::continuePlay()
	{
		if (pVoice == nullptr) return;
		isPaused = false;
		pVoice->Resume();
	    methodChannel->InvokeMethod("speak.onContinue", NULL);
	}
	void FlutterTtsPlugin::stop()
	{
		completeSpeakResult(0);
		if (addWaitHandle != nullptr) {
			UnregisterWaitEx(addWaitHandle, INVALID_HANDLE_VALUE);
			addWaitHandle = nullptr;
		}
		if (pVoice != nullptr) {
			pVoice->Speak(L"", SPF_PURGEBEFORESPEAK, NULL);
			pVoice->Resume();
		}
		isPaused = false;
	    methodChannel->InvokeMethod("speak.onCancel", NULL);
	}
	void FlutterTtsPlugin::setVolume(const double newVolume)
	{
		if (pVoice == nullptr) return;
		const double clampedVolume = std::clamp(newVolume, 0.0, 1.0);
		const USHORT volume = static_cast<USHORT>(100 * clampedVolume);
		pVoice->SetVolume(volume);
	}
	void FlutterTtsPlugin::setPitch(const double newPitch) {pitch = std::clamp(newPitch, 0.0, 2.0);}
	void FlutterTtsPlugin::setRate(const double newRate)
	{
		if (pVoice == nullptr) return;
		const double clampedRate = std::clamp(newRate, 0.0, 1.0);
		const long speechRate = static_cast<long>((clampedRate - 0.5) * 15);
		pVoice->SetRate(speechRate);
	}
	void FlutterTtsPlugin::getVoices(flutter::EncodableList& voices) {
		CComPtr<IEnumSpObjectTokens> cpEnum;
		if (FAILED(SpEnumTokens(SPCAT_VOICES, NULL, NULL, &cpEnum)) || !cpEnum) {
			return;
		}

		ULONG ulCount = 0;
		if (FAILED(cpEnum->GetCount(&ulCount))) {
			return;
		}

		while (ulCount--) {
			CComPtr<ISpObjectToken> cpVoiceToken;
			if (FAILED(cpEnum->Next(1, &cpVoiceToken, NULL)) || !cpVoiceToken) {
				break;
			}

			std::string language;
			if (!GetTokenLanguage(cpVoiceToken, language)) {
				continue;
			}

			std::string name;
			GetTokenAttribute(cpVoiceToken, L"Name", name);

			flutter::EncodableMap voiceInfo;
			voiceInfo[flutter::EncodableValue("locale")] = language;
			voiceInfo[flutter::EncodableValue("name")] = name;

			std::string identifier;
			if (GetTokenId(cpVoiceToken, identifier)) {
				voiceInfo[flutter::EncodableValue("identifier")] = identifier;
			}

			std::string gender;
			if (GetTokenAttribute(cpVoiceToken, L"Gender", gender)) {
				std::transform(gender.begin(), gender.end(), gender.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				voiceInfo[flutter::EncodableValue("gender")] = gender;
			}

			voices.push_back(flutter::EncodableMap(voiceInfo));
		}
	}
	void FlutterTtsPlugin::setVoice(const std::string voiceLanguage, const std::string voiceName, FlutterResult& result) {
		if (pVoice == nullptr) {
			result->Success(0);
			return;
		}

		CComPtr<IEnumSpObjectTokens> cpEnum;
		if (FAILED(SpEnumTokens(SPCAT_VOICES, NULL, NULL, &cpEnum)) || !cpEnum) {
			result->Success(0);
			return;
		}

		ULONG ulCount = 0;
		if (FAILED(cpEnum->GetCount(&ulCount))) {
			result->Success(0);
			return;
		}

		bool success = false;
		while (ulCount--) {
			CComPtr<ISpObjectToken> cpVoiceToken;
			if (FAILED(cpEnum->Next(1, &cpVoiceToken, NULL)) || !cpVoiceToken) {
				break;
			}

			std::string language;
			std::string name;
			if (!GetTokenLanguage(cpVoiceToken, language) ||
				!GetTokenAttribute(cpVoiceToken, L"Name", name)) {
				continue;
			}

			if (name == voiceName && language == voiceLanguage) {
				pVoice->SetVoice(cpVoiceToken);
				success = true;
				break;
			}
		}
		result->Success(success ? 1 : 0);
	}
	void FlutterTtsPlugin::getLanguages(flutter::EncodableList& languages)
	{
		CComPtr<IEnumSpObjectTokens> cpEnum;
		if (FAILED(SpEnumTokens(SPCAT_VOICES, NULL, NULL, &cpEnum)) || !cpEnum) {
			return;
		}

		ULONG ulCount = 0;
		if (FAILED(cpEnum->GetCount(&ulCount))) {
			return;
		}

		std::set<flutter::EncodableValue> languagesSet = {};
		while (ulCount--) {
			CComPtr<ISpObjectToken> cpVoiceToken;
			if (FAILED(cpEnum->Next(1, &cpVoiceToken, NULL)) || !cpVoiceToken) {
				break;
			}

			std::string language;
			if (GetTokenLanguage(cpVoiceToken, language) && !language.empty()) {
				languagesSet.insert(flutter::EncodableValue(language));
			}
		}
		std::for_each(begin(languagesSet), end(languagesSet), [&languages](const flutter::EncodableValue value)
			{
				languages.push_back(value);
			});
	}

	void FlutterTtsPlugin::setLanguage(const std::string voiceLanguage, FlutterResult& result) {
		if (pVoice == nullptr) {
			result->Success(0);
			return;
		}

		CComPtr<IEnumSpObjectTokens> cpEnum;
		if (FAILED(SpEnumTokens(SPCAT_VOICES, NULL, NULL, &cpEnum)) || !cpEnum) {
			result->Success(0);
			return;
		}

		ULONG ulCount = 0;
		if (FAILED(cpEnum->GetCount(&ulCount))) {
			result->Success(0);
			return;
		}

		bool found = false;
		while (ulCount--) {
			CComPtr<ISpObjectToken> cpVoiceToken;
			if (FAILED(cpEnum->Next(1, &cpVoiceToken, NULL)) || !cpVoiceToken) {
				break;
			}

			std::string language;
			if (!GetTokenLanguage(cpVoiceToken, language)) {
				continue;
			}

			const bool matches = language == voiceLanguage ||
				(voiceLanguage.find('-') == std::string::npos &&
				 language.rfind(voiceLanguage + "-", 0) == 0);
			if (matches) {
				pVoice->SetVoice(cpVoiceToken);
				found = true;
				break;
			}
		}
		if (found) result->Success(1);
		else result->Success(0);
	}


	void FlutterTtsPlugin::HandleMethodCall(
		const flutter::MethodCall<flutter::EncodableValue>& method_call,
		FlutterResult result) {

		if (method_call.method_name().compare("getPlatformVersion") == 0) {
			std::ostringstream version_stream;
			version_stream << "Windows ";
			if (IsWindows10OrGreater()) {
				version_stream << "10+";
			}
			else if (IsWindows8OrGreater()) {
				version_stream << "8";
			}
			else if (IsWindows7OrGreater()) {
				version_stream << "7";
			}
			result->Success(flutter::EncodableValue(version_stream.str()));
			return;
		}
#endif
		else if (method_call.method_name().compare("awaitSpeakCompletion") == 0) {
			const auto* arg = GetArgument(method_call, 0);
			if (arg != nullptr && std::holds_alternative<bool>(*arg)) {
				awaitSpeakCompletion = std::get<bool>(*arg);
				result->Success(1);
			}
			else result->Success(0);
		}
		else if (method_call.method_name().compare("speak") == 0) {
			if (isPaused) { continuePlay(); result->Success(1); return; }
			const auto* arg = GetArgument(method_call, 0);
			if (arg != nullptr && std::holds_alternative<std::string>(*arg)) {
				if (!speaking()) {
					const std::string text = std::get<std::string>(*arg);
					speak(text, std::move(result));
				}
				else result->Success(0);
			}
			else result->Success(0);
		}
		else if (method_call.method_name().compare("pause") == 0) {
			FlutterTtsPlugin::pause();
			result->Success(1);
		}
		else if (method_call.method_name().compare("setLanguage") == 0) {
			const auto* arg = GetArgument(method_call, 0);
			if (arg != nullptr && std::holds_alternative<std::string>(*arg)) {
				const std::string lang = std::get<std::string>(*arg);
				setLanguage(lang, result);
			}
			else result->Success(0);
		}
		else if (method_call.method_name().compare("setVolume") == 0) {
			const auto* arg = GetArgument(method_call, 0);
			if (arg != nullptr && std::holds_alternative<double>(*arg)) {
				const double newVolume = std::get<double>(*arg);
				setVolume(newVolume);
				result->Success(1);
			}
			else result->Success(0);
		}
		else if (method_call.method_name().compare("setSpeechRate") == 0) {
			const auto* arg = GetArgument(method_call, 0);
			if (arg != nullptr && std::holds_alternative<double>(*arg)) {
				const double newRate = std::get<double>(*arg);
				setRate(newRate);
				result->Success(1);
			}
			else result->Success(0);
		}
		else if (method_call.method_name().compare("setPitch") == 0) {
			const auto* arg = GetArgument(method_call, 0);
			if (arg != nullptr && std::holds_alternative<double>(*arg)) {
				const double newPitch = std::get<double>(*arg);
				setPitch(newPitch);
				result->Success(1);
			}
			else result->Success(0);
		}
		else if (method_call.method_name().compare("setVoice") == 0) {
			const auto* arg = GetArgument(method_call, 0);
			if (arg != nullptr && std::holds_alternative<flutter::EncodableMap>(*arg)) {
				const flutter::EncodableMap voiceInfo = std::get<flutter::EncodableMap>(*arg);
				std::string voiceLanguage = "";
				std::string voiceName = "";
				auto voiceLanguage_it = voiceInfo.find(flutter::EncodableValue("locale"));
				if (voiceLanguage_it != voiceInfo.end() &&
					std::holds_alternative<std::string>(voiceLanguage_it->second)) {
					voiceLanguage = std::get<std::string>(voiceLanguage_it->second);
				}
				auto voiceName_it = voiceInfo.find(flutter::EncodableValue("name"));
				if (voiceName_it != voiceInfo.end() &&
					std::holds_alternative<std::string>(voiceName_it->second)) {
					voiceName = std::get<std::string>(voiceName_it->second);
				}
				setVoice(voiceLanguage, voiceName, result);
			}
			else result->Success(0);
		}
		else if (method_call.method_name().compare("stop") == 0) {
			stop();
			result->Success(1);
		}
		else if (method_call.method_name().compare("getLanguages") == 0) {
			flutter::EncodableList l;
			getLanguages(l);
			result->Success(l);
		}
		else if (method_call.method_name().compare("getVoices") == 0) {
			flutter::EncodableList l;
			getVoices(l);
			result->Success(l);
		}
		else {
			result->NotImplemented();
		}
	}
}

void FlutterTtsPluginRegisterWithRegistrar(
	FlutterDesktopPluginRegistrarRef registrar) {
	FlutterTtsPlugin::RegisterWithRegistrar(
		flutter::PluginRegistrarManager::GetInstance()
		->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
