#pragma once

#include <fake-jni/fake-jni.h>
#include "java_types.h"
#include "../text_input_handler.h"

class BuildVersion : public FakeJni::JObject {

public:
    DEFINE_CLASS_NAME("android/os/Build$VERSION")

    static FakeJni::JInt SDK_INT;

};

class Context : public FakeJni::JObject {

public:
    DEFINE_CLASS_NAME("android/content/Context")

    virtual std::shared_ptr<File> getFilesDir() = 0;

    virtual std::shared_ptr<File> getCacheDir() = 0;

    std::shared_ptr<ClassLoader> getClassLoader() {
        return ClassLoader::getInstance();
    }

    std::shared_ptr<Context> getApplicationContext() {
        return std::static_pointer_cast<Context>(shared_from_this());
    }

};

class ContextWrapper : public Context {

public:
    DEFINE_CLASS_NAME("android/content/ContextWrapper", Context)

};

class NativeActivity : public ContextWrapper {

public:
    DEFINE_CLASS_NAME("android/app/NativeActivity", ContextWrapper)

};

class HardwareInfo : public FakeJni::JObject {

public:
    DEFINE_CLASS_NAME("com/mojang/minecraftpe/HardwareInformation")

    static std::shared_ptr<FakeJni::JString> getAndroidVersion() {
        return std::make_shared<FakeJni::JString>("Linux");
    }

};

class MainActivity : public NativeActivity {

public:
    DEFINE_CLASS_NAME("com/mojang/minecraftpe/MainActivity", NativeActivity)

    std::string storageDirectory;
    TextInputHandler *textInput = nullptr;
    std::function<void ()> quitCallback;

    int getAndroidVersion() {
        return 27;
    }

    std::shared_ptr<FakeJni::JString> getLocale() {
        return std::make_shared<FakeJni::JString>("en");
    }

    std::shared_ptr<FakeJni::JString> getDeviceModel() {
        return std::make_shared<FakeJni::JString>("Linux");
    }

    std::shared_ptr<File> getFilesDir() override {
        return std::make_shared<File>(storageDirectory);
    }

    std::shared_ptr<File> getCacheDir() override {
        return std::make_shared<File>(storageDirectory);
    }

    std::shared_ptr<FakeJni::JString> getExternalStoragePath() {
        return std::make_shared<FakeJni::JString>(storageDirectory);
    }

    FakeJni::JBoolean hasWriteExternalStoragePermission() {
        return true;
    }

    std::shared_ptr<HardwareInfo> getHardwareInfo() {
        return std::make_shared<HardwareInfo>();
    }

    std::shared_ptr<FakeJni::JString> createUUID();

    std::shared_ptr<FakeJni::JByteArray> getFileDataBytes(std::shared_ptr<FakeJni::JString> path) {
        return std::make_shared<FakeJni::JByteArray>();
    }

    std::shared_ptr<FakeJni::JArray<FakeJni::JString>> getIPAddresses() {
        return std::make_shared<FakeJni::JArray<FakeJni::JString>>();
    }

    std::shared_ptr<FakeJni::JArray<FakeJni::JString>> getBroadcastAddresses() {
        return std::make_shared<FakeJni::JArray<FakeJni::JString>>();
    }

    void showKeyboard(std::shared_ptr<FakeJni::JString> text, FakeJni::JInt maxLen, FakeJni::JBoolean ignored,
            FakeJni::JBoolean ignored2, FakeJni::JBoolean multiline) {
        if (textInput)
            textInput->enable(text->asStdString(), multiline);
    }

    void hideKeyboard() {
        if (textInput)
            textInput->disable();
    }

    void updateTextboxText(std::shared_ptr<FakeJni::JString> newText) {
        if (textInput)
            textInput->update(newText->asStdString());
    }

    int getCursorPosition() {
        if (textInput)
            return textInput->getCursorPosition();
        return 0;
    }

};

class JellyBeanDeviceManager : public FakeJni::JObject {

public:
    DEFINE_CLASS_NAME("com/mojang/minecraftpe/input/JellyBeanDeviceManager")

};