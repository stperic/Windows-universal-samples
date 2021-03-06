//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "pch.h"
#include "Scenario_ContinuousRecognitionSRGSGrammar.xaml.h"
#include "AudioCapturePermissions.h"

using namespace SDKTemplate;
using namespace Concurrency;
using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Resources::Core;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Storage;
using namespace Windows::UI;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;

Scenario_ContinuousRecognitionSRGSGrammar::Scenario_ContinuousRecognitionSRGSGrammar() : rootPage(MainPage::Current)
{
    InitializeComponent();

    colorLookup = ref new Map<String^, Color>(
    {
        { "COLOR_RED",   Colors::Red },    { "COLOR_BLUE",  Colors::Blue },   { "COLOR_BLACK",  Colors::Black },
        { "COLOR_BROWN", Colors::Brown },  { "COLOR_PURPLE",Colors::Purple }, { "COLOR_GREEN",  Colors::Green },
        { "COLOR_YELLOW",Colors::Yellow }, { "COLOR_CYAN",  Colors::Cyan },   { "COLOR_MAGENTA",Colors::Magenta },
        { "COLOR_ORANGE",Colors::Orange }, { "COLOR_GRAY",  Colors::Gray },   { "COLOR_WHITE",  Colors::White }
    });
}

/// <summary>
/// Upon entering the scenario, ensure that we have permissions to use the Microphone. This may entail popping up
/// a dialog to the user on Desktop systems. Only enable functionality once we've gained that permission in order to 
/// prevent errors from occurring when using the SpeechRecognizer. If speech is not a primary input mechanism, developers
/// should consider disabling appropriate parts of the UI if the user does not have a recording device, or does not allow 
/// audio input.
/// </summary>
/// <param name="e">Unused navigation parameters</param>
void Scenario_ContinuousRecognitionSRGSGrammar::OnNavigatedTo(NavigationEventArgs^ e)
{
    Page::OnNavigatedTo(e);

    // Save the UI thread dispatcher to allow speech status messages to be shown on the UI.
    dispatcher = CoreWindow::GetForCurrentThread()->Dispatcher;

    create_task(AudioCapturePermissions::RequestMicrophonePermissionAsync(), task_continuation_context::use_current())
        .then([this](bool permissionGained)
    {
        if (permissionGained)
        {
            // Enable the recognition buttons.
            this->btnContinuousRecognize->IsEnabled = true;
        }
        else
        {
            this->resultTextBlock->Visibility = Windows::UI::Xaml::Visibility::Visible;
            this->resultTextBlock->Text = L"Permission to access capture resources was not given by the user; please set the application setting in Settings->Privacy->Microphone.";
        }
    }).then([this]()
    {
        Windows::Globalization::Language^ speechLanguage = SpeechRecognizer::SystemSpeechLanguage;
        speechContext = ResourceContext::GetForCurrentView();
        speechContext->Languages = ref new VectorView<String^>(1, speechLanguage->LanguageTag);

        speechResourceMap = ResourceManager::Current->MainResourceMap->GetSubtree(L"LocalizationSpeechResources");

        PopulateLanguageDropdown();
        InitializeRecognizer(SpeechRecognizer::SystemSpeechLanguage);
    }, task_continuation_context::use_current());
}

/// <summary>
/// Creates a SpeechRecognizer instance and initializes the grammar.
/// </summary>
void Scenario_ContinuousRecognitionSRGSGrammar::InitializeRecognizer(Windows::Globalization::Language^ recognizerLanguage)
{
    // If reinitializing the recognizer (ie, changing the speech language), clean up the old recognizer first.
    // Avoid doing this while the recognizer is active by disabling the ability to change languages while listening.
    if (this->speechRecognizer != nullptr)
    {
        speechRecognizer->StateChanged -= stateChangedToken;
        speechRecognizer->ContinuousRecognitionSession->Completed -= continuousRecognitionCompletedToken;
        speechRecognizer->ContinuousRecognitionSession->ResultGenerated -= continuousRecognitionResultGeneratedToken;

        delete this->speechRecognizer;
        this->speechRecognizer = nullptr;
    }

    try
    {
        // Initialize the SpeechRecognizer and add the grammar.
        this->speechRecognizer = ref new SpeechRecognizer(recognizerLanguage);

        // Provide feedback to the user about the state of the recognizer. This can be used to provide
        // visual feedback to help the user understand whether they're being heard.
        stateChangedToken = speechRecognizer->StateChanged +=
            ref new TypedEventHandler<
            SpeechRecognizer ^,
            SpeechRecognizerStateChangedEventArgs ^>(
                this,
                &Scenario_ContinuousRecognitionSRGSGrammar::SpeechRecognizer_StateChanged);

        // Initialize the SRGS-compliant XML file.
        // For more information about grammars for Windows apps and how to
        // define and use SRGS-compliant grammars in your app, see
        // https://msdn.microsoft.com/en-us/library/dn596121.aspx
        String^ fileName = L"SRGS\\" + recognizerLanguage->LanguageTag + L"\\SRGSColors.xml";
        resultTextBlock->Text = speechResourceMap->GetValue("SRGSHelpText", speechContext)->ValueAsString;

        create_task(Package::Current->InstalledLocation->GetFileAsync(fileName), task_continuation_context::use_current())
            .then([this](task<StorageFile^> getFileTask)
        {
            StorageFile^ grammarContentFile = getFileTask.get();
            SpeechRecognitionGrammarFileConstraint^ grammarConstraint = ref new SpeechRecognitionGrammarFileConstraint(grammarContentFile);
            speechRecognizer->Constraints->Append(grammarConstraint);

            return create_task(speechRecognizer->CompileConstraintsAsync(), task_continuation_context::use_current());

        }).then([this](task<SpeechRecognitionCompilationResult^> previousTask)
        {
            SpeechRecognitionCompilationResult^ compilationResult = previousTask.get();

            // Check to make sure that the constraints were in a proper format and the recognizer was able to compile them.
            if (compilationResult->Status != SpeechRecognitionResultStatus::Success)
            {
                // Disable the recognition button.
                btnContinuousRecognize->IsEnabled = false;

                // Let the user know that the grammar didn't compile properly.
                rootPage->NotifyUser("Grammar Compilation Failed: " + compilationResult->Status.ToString(), NotifyType::ErrorMessage);
            }
            else
            {
                btnContinuousRecognize->IsEnabled = true;

                resultTextBlock->Text = speechResourceMap->GetValue("SRGSHelpText", speechContext)->ValueAsString;
                resultTextBlock->Visibility = Windows::UI::Xaml::Visibility::Visible;

                // Set EndSilenceTimeout to give users more time to complete speaking a phrase.
                TimeSpan endSilenceTime;
                endSilenceTime.Duration = 12000000L;
                speechRecognizer->Timeouts->EndSilenceTimeout = endSilenceTime; // (1.2 seconds in nanoseconds)


                                                                                // Handle continuous recognition events. Completed fires when various error states occur. ResultGenerated fires when
                                                                                // some recognized phrases occur, or the garbage rule is hit.
                continuousRecognitionCompletedToken = speechRecognizer->ContinuousRecognitionSession->Completed +=
                    ref new TypedEventHandler<
                    SpeechContinuousRecognitionSession ^,
                    SpeechContinuousRecognitionCompletedEventArgs ^>(
                        this,
                        &Scenario_ContinuousRecognitionSRGSGrammar::ContinuousRecognitionSession_Completed);
                continuousRecognitionResultGeneratedToken = speechRecognizer->ContinuousRecognitionSession->ResultGenerated +=
                    ref new TypedEventHandler<
                    SpeechContinuousRecognitionSession ^,
                    SpeechContinuousRecognitionResultGeneratedEventArgs ^>(
                        this,
                        &Scenario_ContinuousRecognitionSRGSGrammar::ContinuousRecognitionSession_ResultGenerated);
            }
        }, task_continuation_context::use_current());

        
    }
    catch (Platform::Exception^ ex)
    {
        if ((unsigned int)ex->HResult == HResultRecognizerNotFound)
        {
            btnContinuousRecognize->IsEnabled = false;

            resultTextBlock->Visibility = Windows::UI::Xaml::Visibility::Visible;
            resultTextBlock->Text = L"Speech Language pack for selected language not installed.";
        }
        else
        {
            auto messageDialog = ref new Windows::UI::Popups::MessageDialog(ex->Message, "Exception");
            create_task(messageDialog->ShowAsync());
        }
    }
}

/// <summary>
/// Upon leaving, clean up the speech recognizer. Ensure we aren't still listening, and disable the event 
/// handlers to prevent leaks.
/// </summary>
/// <param name="e">Unused navigation parameters.</param>
void Scenario_ContinuousRecognitionSRGSGrammar::OnNavigatedFrom(NavigationEventArgs^ e)
{
    Page::OnNavigatedFrom(e);

    if (speechRecognizer != nullptr)
    {
        // If we're currently active, start a cancellation task, and wait for it to finish before shutting down
        // the recognizer.
        Concurrency::task<void> cleanupTask;
        if (speechRecognizer->State != SpeechRecognizerState::Idle)
        {
            cleanupTask = create_task(speechRecognizer->ContinuousRecognitionSession->CancelAsync(), task_continuation_context::use_current());
        }
        else
        {
            cleanupTask = create_task([]() {}, task_continuation_context::use_current());
        }

        cleanupTask.then([this]()
        {
            ContinuousRecoButtonText->Text = L" Continuous Recognition";

            speechRecognizer->StateChanged -= stateChangedToken;
            speechRecognizer->ContinuousRecognitionSession->Completed -= continuousRecognitionCompletedToken;
            speechRecognizer->ContinuousRecognitionSession->ResultGenerated -= continuousRecognitionResultGeneratedToken;

            delete speechRecognizer;
            speechRecognizer = nullptr;
        }, task_continuation_context::use_current());
    }
}

/// <summary>
/// Begin recognition or finish the recognition session.
/// </summary>
/// <param name="sender">The button that generated this event</param>
/// <param name="e">Unused event details</param>
void Scenario_ContinuousRecognitionSRGSGrammar::ContinuousRecognize_Click(Object^ sender, RoutedEventArgs^ e)
{
    // The recognizer can only start listening in a continuous fashion if the recognizer is currently idle.
    // This prevents an exception from occurring.
    if (speechRecognizer->State == SpeechRecognizerState::Idle)
    {
        // Reset the text to prompt the user.
        ContinuousRecoButtonText->Text = L" Stop Continuous Recognition";
        resultTextBlock->Text = speechResourceMap->GetValue("SRGSHelpText", speechContext)->ValueAsString;
        cbLanguageSelection->IsEnabled = false;

        create_task(speechRecognizer->ContinuousRecognitionSession->StartAsync(), task_continuation_context::use_current())
            .then([this](task<void> startAsyncTask)
        {
            try
            {
                // Retreive any exceptions that may have been generated by StartAsync.
                startAsyncTask.get();
            }
            catch (Exception^ exception)
            {
                ContinuousRecoButtonText->Text = L" Continuous Recognition";
                cbLanguageSelection->IsEnabled = true;
                resultTextBlock->Text = speechResourceMap->GetValue("SRGSHelpText", speechContext)->ValueAsString;

                auto messageDialog = ref new Windows::UI::Popups::MessageDialog(exception->Message, "Exception");
                create_task(messageDialog->ShowAsync());
            }
        });
    }
    else
    {
        // Reset the text to prompt the user.
        ContinuousRecoButtonText->Text = L" Continuous Recognition";
        resultTextBlock->Text = speechResourceMap->GetValue("SRGSHelpText", speechContext)->ValueAsString;
        cbLanguageSelection->IsEnabled = true;

        // Cancelling recognition prevents any currently recognized speech from
        // generating a ResultGenerated event. StopAsync() will allow the final session to 
        // complete
        create_task(speechRecognizer->ContinuousRecognitionSession->CancelAsync());
    }
}

/// <summary>
/// Provide feedback to the user based on whether the recognizer is receiving their voice input.
/// </summary>
/// <param name="sender">The recognizer that is currently running.</param>
/// <param name="args">The current state of the recognizer.</param>
void Scenario_ContinuousRecognitionSRGSGrammar::SpeechRecognizer_StateChanged(SpeechRecognizer ^sender, SpeechRecognizerStateChangedEventArgs ^args)
{
    dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, args]()
    {
        rootPage->NotifyUser("Speech recognizer state: " + args->State.ToString(), NotifyType::StatusMessage);
    }));

}

/// <summary>
/// Handle events fired when the session ends, either from a call to
/// CancelAsync() or StopAsync(), or an error condition, such as the 
/// microphone becoming unavailable or some transient issues occuring.
/// </summary>
/// <param name="sender">The continuous recognition session</param>
/// <param name="args">The state of the recognizer</param>
void Scenario_ContinuousRecognitionSRGSGrammar::ContinuousRecognitionSession_Completed(SpeechContinuousRecognitionSession ^sender, SpeechContinuousRecognitionCompletedEventArgs ^args)
{
    if (args->Status != SpeechRecognitionResultStatus::Success)
    {
        dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, args]()
        {
            rootPage->NotifyUser("Continuous Recognition Completed: " + args->Status.ToString(), NotifyType::ErrorMessage);
            ContinuousRecoButtonText->Text = " Continuous Recognition";
        }));
    }
}

/// <summary>
/// Handle events fired when a result is generated. This may include a garbage rule that fires when general room noise
/// or side-talk is captured (this will have a confidence of Rejected typically, but may occasionally match a rule with
/// low confidence).
/// </summary>
/// <param name="sender">The Recognition session that generated this result</param>
/// <param name="args">Details about the recognized speech</param>
void Scenario_ContinuousRecognitionSRGSGrammar::ContinuousRecognitionSession_ResultGenerated(SpeechContinuousRecognitionSession ^sender, SpeechContinuousRecognitionResultGeneratedEventArgs ^args)
{
    dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, args]()
    {
        HandleRecognitionResult(args->Result);
    }));
}


/// <summary>
/// Uses the result from the speech recognizer to change the colors of the shapes.
/// </summary>
/// <param name="recoResult">The result from the recognition event</param>
void Scenario_ContinuousRecognitionSRGSGrammar::HandleRecognitionResult(SpeechRecognitionResult^ recoResult)
{
    if (recoResult->Confidence == SpeechRecognitionConfidence::High ||
        recoResult->Confidence == SpeechRecognitionConfidence::Medium)
    {
        String^ garbagePrompt = "";


        // BACKGROUND: Check to see if the recognition result contains the semantic key for the background color,
        // and not a match for the GARBAGE rule, and change the color.
        if (recoResult->SemanticInterpretation->Properties->HasKey("KEY_BACKGROUND") && recoResult->SemanticInterpretation->Properties->Lookup("KEY_BACKGROUND")->GetAt(0) != "...")
        {
            String^ backgroundColor = recoResult->SemanticInterpretation->Properties->Lookup("KEY_BACKGROUND")->GetAt(0);
            colorRectangle->Fill = ref new SolidColorBrush(getColor(backgroundColor));
        }

        // If "background" was matched, but the color rule matched GARBAGE, prompt the user.
        else if (recoResult->SemanticInterpretation->Properties->HasKey("KEY_BACKGROUND") && recoResult->SemanticInterpretation->Properties->Lookup("KEY_BACKGROUND")->GetAt(0) == "...")
        {
            garbagePrompt += speechResourceMap->GetValue("SRGSBackgroundGarbagePromptText", speechContext)->ValueAsString;
            resultTextBlock->Text = garbagePrompt;
        }

        // BORDER: Check to see if the recognition result contains the semantic key for the border color,
        // and not a match for the GARBAGE rule, and change the color.
        if (recoResult->SemanticInterpretation->Properties->HasKey("KEY_BORDER") && recoResult->SemanticInterpretation->Properties->Lookup("KEY_BORDER")->GetAt(0) != "...")
        {
            String^ borderColor = recoResult->SemanticInterpretation->Properties->Lookup("KEY_BORDER")->GetAt(0);
            colorRectangle->Stroke = ref new SolidColorBrush(getColor(borderColor));
        }

        // If "border" was matched, but the color rule matched GARBAGE, prompt the user.
        else if (recoResult->SemanticInterpretation->Properties->HasKey("KEY_BORDER") && recoResult->SemanticInterpretation->Properties->Lookup("KEY_BORDER")->GetAt(0) == "...")
        {
            garbagePrompt += speechResourceMap->GetValue("SRGSBorderGarbagePromptText", speechContext)->ValueAsString;
            resultTextBlock->Text = garbagePrompt;
        }

        // CIRCLE: Check to see if the recognition result contains the semantic key for the circle color,
        // and not a match for the GARBAGE rule, and change the color.
        if (recoResult->SemanticInterpretation->Properties->HasKey("KEY_CIRCLE") && recoResult->SemanticInterpretation->Properties->Lookup("KEY_CIRCLE")->GetAt(0) != "...")
        {
            String^ circleColor = recoResult->SemanticInterpretation->Properties->Lookup("KEY_CIRCLE")->GetAt(0);
            colorCircle->Fill = ref new SolidColorBrush(getColor(circleColor));
        }

        // If "circle" was matched, but the color rule matched GARBAGE, prompt the user.
        else if (recoResult->SemanticInterpretation->Properties->HasKey("KEY_CIRCLE") && recoResult->SemanticInterpretation->Properties->Lookup("KEY_CIRCLE")->GetAt(0) == "...")
        {
            garbagePrompt += speechResourceMap->GetValue("SRGSCircleGarbagePromptText", speechContext)->ValueAsString;
            resultTextBlock->Text = garbagePrompt;
        }

        // Initialize a string that will describe the user's color choices.
        String^ textBoxColors = "You selected (Semantic Properties) -> \n";

        std::for_each(begin(recoResult->SemanticInterpretation->Properties),
            end(recoResult->SemanticInterpretation->Properties),
            [&](IKeyValuePair<String^, IVectorView<String^>^>^ child)
        {
            // Check to see if any of the semantic values in recognition result contains a match for the GARBAGE rule.
            if (!child->Value->Equals("..."))
            {
                textBoxColors += child->Value->GetAt(0) + " ";
                if (child->Key != nullptr)
                {
                    textBoxColors += child->Key;
                }
                else
                {
                    textBoxColors += "null";
                }
                textBoxColors += "\n";

                resultTextBlock->Text = textBoxColors;
            }

            // If there was no match to the colors rule or if it matched GARBAGE, prompt the user.
            else
            {
                resultTextBlock->Text = garbagePrompt;
            }
        });
    }
    else
    {
        resultTextBlock->Text = speechResourceMap->GetValue("SRGSGarbagePromptText", speechContext)->ValueAsString;
    }
}


/// <summary>
/// Creates a color object from the passed in string.
/// </summary>
/// <param name="colorString">The name of the color</param>
Windows::UI::Color Scenario_ContinuousRecognitionSRGSGrammar::getColor(Platform::String^ colorString)
{
    Color newColor = Colors::Transparent;

    if (colorLookup->HasKey(colorString))
    {
        newColor = colorLookup->Lookup(colorString);
    }

    return newColor;
}


/// <summary>
/// Populate language dropdown with supported Dictation languages.
/// </summary>
void Scenario_ContinuousRecognitionSRGSGrammar::PopulateLanguageDropdown()
{    
    // disable callback temporarily.
    cbLanguageSelection->SelectionChanged -= cbLanguageSelectionSelectionChangedToken;

    Windows::Globalization::Language^ defaultLanguage = SpeechRecognizer::SystemSpeechLanguage;
    auto supportedLanguages = SpeechRecognizer::SupportedGrammarLanguages;
    std::for_each(begin(supportedLanguages), end(supportedLanguages), [&](Windows::Globalization::Language^ lang)
    {
        ComboBoxItem^ item = ref new ComboBoxItem();
        item->Tag = lang;
        item->Content = lang->DisplayName;

        cbLanguageSelection->Items->Append(item);
        if (lang->LanguageTag == defaultLanguage->LanguageTag)
        {
            item->IsSelected = true;
            cbLanguageSelection->SelectedItem = item;
        }
    });    
    
    cbLanguageSelectionSelectionChangedToken = cbLanguageSelection->SelectionChanged +=
        ref new SelectionChangedEventHandler(this, &Scenario_ContinuousRecognitionSRGSGrammar::cbLanguageSelection_SelectionChanged);
}

/// <summary>
/// Re-initialize the recognizer based on selections from the language combobox.
/// </summary>
void Scenario_ContinuousRecognitionSRGSGrammar::cbLanguageSelection_SelectionChanged(Object^ sender, SelectionChangedEventArgs^ e)
{
    ComboBoxItem^ item = (ComboBoxItem^)(cbLanguageSelection->SelectedItem);
    Windows::Globalization::Language^ newLanguage = (Windows::Globalization::Language^)item->Tag;

    if (this->speechRecognizer != nullptr)
    {
        if (speechRecognizer->CurrentLanguage == newLanguage)
        {
            return;
        }
    }

    try
    {
        speechContext->Languages = ref new VectorView<String^>(1, newLanguage->LanguageTag);

        InitializeRecognizer(newLanguage);
    }
    catch (Exception^ exception)
    {
        auto messageDialog = ref new Windows::UI::Popups::MessageDialog(exception->Message, "Exception");
        create_task(messageDialog->ShowAsync());
    }

}