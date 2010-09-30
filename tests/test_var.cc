// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/tests/test_var.h"

#include <string.h>

#include <limits>

#include "ppapi/c/dev/ppb_testing_dev.h"
#include "ppapi/c/ppb_var.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/var.h"
#include "ppapi/tests/testing_instance.h"

static uint32_t kInvalidLength = static_cast<uint32_t>(-1);

REGISTER_TEST_CASE(Var);

bool TestVar::Init() {
  var_interface_ = reinterpret_cast<PPB_Var const*>(
      pp::Module::Get()->GetBrowserInterface(PPB_VAR_INTERFACE));
  testing_interface_ = reinterpret_cast<PPB_Testing_Dev const*>(
      pp::Module::Get()->GetBrowserInterface(PPB_TESTING_DEV_INTERFACE));
  if (!testing_interface_) {
    // Give a more helpful error message for the testing interface being gone
    // since that needs special enabling in Chrome.
    instance_->AppendError("This test needs the testing interface, which is "
        "not currently available. In Chrome, use --enable-pepper-testing when "
        "launching.");
  }
  return var_interface_ && testing_interface_;
}

void TestVar::RunTest() {
  RUN_TEST(BasicString);
  RUN_TEST(InvalidAndEmpty);
  RUN_TEST(InvalidUtf8);
  RUN_TEST(NullInputInUtf8Conversion);
  RUN_TEST(ValidUtf8);
  RUN_TEST(Utf8WithEmbeddedNulls);
  RUN_TEST(VarToUtf8ForWrongType);
  RUN_TEST(HasPropertyAndMethod);
}

std::string TestVar::TestBasicString() {
  uint32_t before_object = testing_interface_->GetLiveObjectCount(
      pp::Module::Get()->pp_module());
  {
    const uint32_t kStrLen = 5;
    const char kStr[kStrLen + 1] = "Hello";
    PP_Var str = var_interface_->VarFromUtf8(pp::Module::Get()->pp_module(),
                                             kStr, sizeof(kStr) - 1);
    ASSERT_TRUE(str.type == PP_VARTYPE_STRING);

    // Reading back the string should work.
    uint32_t len = 0;
    const char* result = var_interface_->VarToUtf8(str, &len);
    ASSERT_TRUE(len == kStrLen);
    ASSERT_TRUE(strncmp(kStr, result, kStrLen) == 0);

    // Destroy the string, readback should now fail.
    var_interface_->Release(str);
    result = var_interface_->VarToUtf8(str, &len);
    ASSERT_TRUE(len == 0);
    ASSERT_TRUE(result == NULL);
  }

  // Make sure nothing leaked.
  ASSERT_TRUE(testing_interface_->GetLiveObjectCount(
      pp::Module::Get()->pp_module()) == before_object);

  return std::string();
}

std::string TestVar::TestInvalidAndEmpty() {
  PP_Var invalid_string;
  invalid_string.type = PP_VARTYPE_STRING;
  invalid_string.value.as_id = 31415926;

  // Invalid strings should give NULL as the return value.
  uint32_t len = std::numeric_limits<uint32_t>::max();
  const char* result = var_interface_->VarToUtf8(invalid_string, &len);
  ASSERT_TRUE(len == 0);
  ASSERT_TRUE(result == NULL);

  // Same with vars that are not strings.
  len = std::numeric_limits<uint32_t>::max();
  pp::Var int_var(42);
  result = var_interface_->VarToUtf8(int_var.pp_var(), &len);
  ASSERT_TRUE(len == 0);
  ASSERT_TRUE(result == NULL);

  // Empty strings should return non-NULL.
  pp::Var empty_string("");
  len = std::numeric_limits<uint32_t>::max();
  result = var_interface_->VarToUtf8(empty_string.pp_var(), &len);
  ASSERT_TRUE(len == 0);
  ASSERT_TRUE(result != NULL);

  return std::string();
}

std::string TestVar::TestInvalidUtf8() {
  // utf8じゃない (japanese for "is not utf8") in shift-jis encoding.
  static const char kSjisString[] = "utf8\x82\xb6\x82\xe1\x82\xc8\x82\xa2";
  pp::Var sjis(kSjisString);
  if (!sjis.is_null())
    return "Non-UTF8 string permitted.";

  return "";
}

std::string TestVar::TestNullInputInUtf8Conversion() {
  // This test talks directly to the C interface to access edge cases that
  // cannot be exercised via the C++ interface.
  PP_Var converted_string;

  // 0-length string should not dereference input string, and should produce
  // an empty string.
  converted_string = var_interface_->VarFromUtf8(
      pp::Module::Get()->pp_module(), NULL, 0);
  if (converted_string.type != PP_VARTYPE_STRING) {
    return "Expected 0 length to return empty string.";
  }

  // Now convert it back.
  uint32_t length = kInvalidLength;
  const char* result = NULL;
  result = var_interface_->VarToUtf8(converted_string, &length);
  if (length != 0) {
    return "Expected 0 length string on conversion.";
  }
  if (result == NULL) {
    return "Expected a non-null result for 0-lengthed string from VarToUtf8.";
  }

  // Should not crash, and make an empty string.
  const char* null_string = NULL;
  pp::Var null_var(null_string);
  if (!null_var.is_string() || null_var.AsString() != "") {
    return "Expected NULL input to make an empty string Var.";
  }

  return "";
}

std::string TestVar::TestValidUtf8() {
  // From UTF8 string -> PP_Var.
  // Chinese for "I am utf8."
  static const char kValidUtf8[] = "\xe6\x88\x91\xe6\x98\xafutf8.";
  pp::Var converted_string(kValidUtf8);

  if (converted_string.is_null())
    return "Unable to convert valid utf8 to var.";

  // Since we're already here, test PP_Var back to UTF8 string.
  std::string returned_string = converted_string.AsString();

  // We need to check against 1 less than sizeof because the resulting string
  // is technically not NULL terminated by API design.
  if (returned_string.size() != sizeof(kValidUtf8) - 1) {
    return "Unable to convert utf8 string back from var.";
  }
  if (returned_string != kValidUtf8) {
    return "String mismatches on conversion back from PP_Var.";
  }

  return "";
}

std::string TestVar::TestUtf8WithEmbeddedNulls() {
  // From UTF8 string with embedded nulls -> PP_Var.
  // Chinese for "also utf8."
  static const char kUtf8WithEmbededNull[] = "\xe6\xb9\x9f\xe6\x98\xaf\0utf8.";
  std::string orig_string(kUtf8WithEmbededNull,
                          sizeof(kUtf8WithEmbededNull) -1);
  pp::Var converted_string(orig_string);

  if (converted_string.is_null())
    return "Unable to convert utf8 with embedded nulls to var.";

  // Since we're already here, test PP_Var back to UTF8 string.
  std::string returned_string = converted_string.AsString();

  if (returned_string.size() != orig_string.size()) {
    return "Unable to convert utf8 with embedded nulls back from var.";
  }
  if (returned_string != orig_string) {
    return "String mismatches on conversion back from PP_Var.";
  }

  return "";
}

std::string TestVar::TestVarToUtf8ForWrongType() {
  uint32_t length = kInvalidLength;
  const char* result = NULL;
  result = var_interface_->VarToUtf8(PP_MakeVoid(), &length);
  if (length != 0) {
    return "Expected 0 on string conversion from Void var.";
  }
  if (result != NULL) {
    return "Expected NULL on string conversion from Void var.";
  }

  length = kInvalidLength;
  result = NULL;
  result = var_interface_->VarToUtf8(PP_MakeNull(), &length);
  if (length != 0) {
    return "Expected 0 on string conversion from Null var.";
  }
  if (result != NULL) {
    return "Expected NULL on string conversion from Null var.";
  }

  length = kInvalidLength;
  result = NULL;
  result = var_interface_->VarToUtf8(PP_MakeBool(true), &length);
  if (length != 0) {
    return "Expected 0 on string conversion from Bool var.";
  }
  if (result != NULL) {
    return "Expected NULL on string conversion from Bool var.";
  }

  length = kInvalidLength;
  result = NULL;
  result = var_interface_->VarToUtf8(PP_MakeInt32(1), &length);
  if (length != 0) {
    return "Expected 0 on string conversion from Int32 var.";
  }
  if (result != NULL) {
    return "Expected NULL on string conversion from Int32 var.";
  }

  length = kInvalidLength;
  result = NULL;
  result = var_interface_->VarToUtf8(PP_MakeDouble(1.0), &length);
  if (length != 0) {
    return "Expected 0 on string conversion from Double var.";
  }
  if (result != NULL) {
    return "Expected NULL on string conversion from Double var.";
  }

  return "";
}

std::string TestVar::TestHasPropertyAndMethod() {
  uint32_t before_objects = testing_interface_->GetLiveObjectCount(
      pp::Module::Get()->pp_module());
  {
    pp::Var window = instance_->GetWindowObject();
    ASSERT_TRUE(window.is_object());

    // Regular property.
    pp::Var exception;
    ASSERT_TRUE(window.HasProperty("scrollX", &exception));
    ASSERT_TRUE(exception.is_void());
    ASSERT_FALSE(window.HasMethod("scrollX", &exception));
    ASSERT_TRUE(exception.is_void());

    // Regular method (also counts as HasProperty).
    ASSERT_TRUE(window.HasProperty("find", &exception));
    ASSERT_TRUE(exception.is_void());
    ASSERT_TRUE(window.HasMethod("find", &exception));
    ASSERT_TRUE(exception.is_void());

    // Nonexistant ones should return false and not set the exception.
    ASSERT_FALSE(window.HasProperty("superEvilBit", &exception));
    ASSERT_TRUE(exception.is_void());
    ASSERT_FALSE(window.HasMethod("superEvilBit", &exception));
    ASSERT_TRUE(exception.is_void());

    // Check exception and return false on invalid property name.
    ASSERT_FALSE(window.HasProperty(3.14159, &exception));
    ASSERT_FALSE(exception.is_void());
    exception = pp::Var();

    exception = pp::Var();
    ASSERT_FALSE(window.HasMethod(3.14159, &exception));
    ASSERT_FALSE(exception.is_void());

    // Try to use something not an object.
    exception = pp::Var();
    pp::Var string_object("asdf");
    ASSERT_FALSE(string_object.HasProperty("find", &exception));
    ASSERT_FALSE(exception.is_void());
    exception = pp::Var();
    ASSERT_FALSE(string_object.HasMethod("find", &exception));
    ASSERT_FALSE(exception.is_void());

    // Try to use an invalid object (need to use the C API).
    PP_Var invalid_object;
    invalid_object.type = PP_VARTYPE_OBJECT;
    invalid_object.value.as_id = static_cast<int64_t>(-1234567);
    PP_Var exception2 = PP_MakeVoid();
    ASSERT_FALSE(var_interface_->HasProperty(invalid_object,
                                             pp::Var("find").pp_var(),
                                             &exception2));
    ASSERT_TRUE(exception2.type != PP_VARTYPE_VOID);
    var_interface_->Release(exception2);

    exception2 = PP_MakeVoid();
    ASSERT_FALSE(var_interface_->HasMethod(invalid_object,
                                           pp::Var("find").pp_var(),
                                           &exception2));
    ASSERT_TRUE(exception2.type != PP_VARTYPE_VOID);
    var_interface_->Release(exception2);

    // Get a valid property/method when the exception is set returns false.
    exception = pp::Var("Bad something-or-other exception");
    ASSERT_FALSE(window.HasProperty("find", &exception));
    ASSERT_FALSE(exception.is_void());
    ASSERT_FALSE(window.HasMethod("find", &exception));
    ASSERT_FALSE(exception.is_void());
  }

  // Make sure nothing leaked.
  ASSERT_TRUE(testing_interface_->GetLiveObjectCount(
      pp::Module::Get()->pp_module()) == before_objects);

  return std::string();
}
