// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/android/android_handler.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/scoped_native_library.h"
#include "jni/AndroidHandler_jni.h"
#include "mojo/common/data_pipe_utils.h"
#include "mojo/public/c/system/main.h"
#include "mojo/public/cpp/application/application_impl.h"
#include "shell/android/run_android_application_function.h"
#include "shell/dynamic_service_runner.h"

using base::android::AttachCurrentThread;
using base::android::ScopedJavaLocalRef;
using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using base::android::GetApplicationContext;

namespace mojo {

namespace {

// This function loads the application library, sets the application context and
// thunks and calls into the application MojoMain. To ensure that the thunks are
// set correctly we keep it in the Mojo shell .so and pass the function pointer
// to the helper libbootstrap.so.
void RunAndroidApplication(JNIEnv* env,
                           jobject j_context,
                           const base::FilePath& app_path,
                           jint j_handle) {
  InterfaceRequest<Application> application_request =
      MakeRequest<Application>(MakeScopedHandle(MessagePipeHandle(j_handle)));

  // Load the library, so that we can set the application context there if
  // needed.
  base::NativeLibraryLoadError error;
  base::ScopedNativeLibrary app_library(
      base::LoadNativeLibrary(app_path, &error));
  if (!app_library.is_valid()) {
    LOG(ERROR) << "Failed to load app library (error: " << error.ToString()
               << ")";
    return;
  }

  // Set the application context if needed. Most applications will need to
  // access the Android ApplicationContext in which they are run. If the
  // application library exports the InitApplicationContext function, we will
  // set it there.
  const char* init_application_context_name = "InitApplicationContext";
  typedef void (*InitApplicationContextFn)(
      const base::android::JavaRef<jobject>&);
  InitApplicationContextFn init_application_context =
      reinterpret_cast<InitApplicationContextFn>(
          app_library.GetFunctionPointer(init_application_context_name));
  if (init_application_context) {
    base::android::ScopedJavaLocalRef<jobject> scoped_context(env, j_context);
    init_application_context(scoped_context);
  }

  // Run the application.
  base::ScopedNativeLibrary app_library_from_runner(
      shell::LoadAndRunNativeApplication(app_path, NativeRunner::DeleteAppPath,
                                         application_request.Pass()));
}

}  // namespace

AndroidHandler::AndroidHandler() : content_handler_factory_(this) {
}

AndroidHandler::~AndroidHandler() {
}

void AndroidHandler::RunApplication(
    InterfaceRequest<Application> application_request,
    URLResponsePtr response) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jstring> j_archive_path =
      Java_AndroidHandler_getNewTempArchivePath(env, GetApplicationContext());
  base::FilePath archive_path(
      ConvertJavaStringToUTF8(env, j_archive_path.obj()));

  common::BlockingCopyToFile(response->body.Pass(), archive_path);
  RunAndroidApplicationFn run_android_application_fn = &RunAndroidApplication;
  Java_AndroidHandler_bootstrap(
      env, GetApplicationContext(), j_archive_path.obj(),
      application_request.PassMessagePipe().release().value(),
      reinterpret_cast<jlong>(run_android_application_fn));
}

void AndroidHandler::Initialize(ApplicationImpl* app) {
}

bool AndroidHandler::ConfigureIncomingConnection(
    ApplicationConnection* connection) {
  connection->AddService(&content_handler_factory_);
  return true;
}

bool RegisterAndroidHandlerJni(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

}  // namespace mojo
