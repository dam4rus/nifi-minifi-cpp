/**
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdlib.h>
#include <iostream>

#include "core/Processor.h"
#include "../TestBase.h"
#include "processors/GetFile.h"
#include "processors/LogAttribute.h"
#include "processors/UpdateAttribute.h"
#include "utils/file/FileUtils.h"
#include "MockS3Wrapper.h"
#include "utils/TestUtils.h"

using org::apache::nifi::minifi::utils::createTempDir;

template<typename T>
class S3TestsFixture {
 public:
  const std::string INPUT_FILENAME = "input_data.log";
  const std::string INPUT_DATA = "input_data";
  const std::string S3_BUCKET = "testBucket";

  S3TestsFixture() {
    // Disable retrieving AWS metadata for tests
    #ifdef WIN32
    _putenv_s("AWS_EC2_METADATA_DISABLED", "true");
    #else
    setenv("AWS_EC2_METADATA_DISABLED", "true", 1);
    #endif

    LogTestController::getInstance().setDebug<TestPlan>();
    LogTestController::getInstance().setDebug<minifi::core::Processor>();
    LogTestController::getInstance().setTrace<minifi::core::ProcessSession>();
    LogTestController::getInstance().setTrace<processors::GetFile>();
    LogTestController::getInstance().setDebug<processors::UpdateAttribute>();
    LogTestController::getInstance().setDebug<processors::LogAttribute>();
    LogTestController::getInstance().setTrace<T>();

    // Build MiNiFi processing graph
    plan = test_controller.createPlan();
    mock_s3_wrapper_ptr = new MockS3Wrapper();
    std::unique_ptr<minifi::aws::s3::S3WrapperBase> mock_s3_wrapper(mock_s3_wrapper_ptr);
    s3_processor = std::shared_ptr<T>(new T("S3Processor", utils::Identifier(), std::move(mock_s3_wrapper)));

    auto input_dir = createTempDir(&test_controller);
    std::ofstream input_file_stream(input_dir + utils::file::FileUtils::get_separator() + INPUT_FILENAME);
    input_file_stream << INPUT_DATA;
    input_file_stream.close();
    get_file = plan->addProcessor("GetFile", "GetFile");
    plan->setProperty(get_file, processors::GetFile::Directory.getName(), input_dir);
    plan->setProperty(get_file, processors::GetFile::KeepSourceFile.getName(), "false");
    update_attribute = plan->addProcessor(
      "UpdateAttribute",
      "UpdateAttribute",
      core::Relationship("success", "d"),
      true);
    plan->addProcessor(
      s3_processor,
      "S3Processor",
      core::Relationship("success", "d"),
      true);
    plan->addProcessor(
      "LogAttribute",
      "LogAttribute",
      core::Relationship("success", "d"),
      true);
    aws_credentials_service = plan->addController("AWSCredentialsService", "AWSCredentialsService");
  }

  void setAccesKeyCredentialsInProcessor() {
    plan->setProperty(update_attribute, "s3.accessKey", "key", true);
    plan->setProperty(s3_processor, "Access Key", "${s3.accessKey}");
    plan->setProperty(update_attribute, "s3.secretKey", "secret", true);
    plan->setProperty(s3_processor, "Secret Key", "${s3.secretKey}");
  }

  void setAccessKeyCredentialsInController() {
    plan->setProperty(aws_credentials_service, "Access Key", "key");
    plan->setProperty(aws_credentials_service, "Secret Key", "secret");
  }

  template<typename Component>
  void setCredentialFile(const Component &component) {
    char in_dir[] = "/tmp/gt.XXXXXX";
    auto temp_path = test_controller.createTempDirectory(in_dir);
    REQUIRE(!temp_path.empty());
    std::string aws_credentials_file(temp_path + utils::file::FileUtils::get_separator() + "aws_creds.conf");
    std::ofstream aws_credentials_file_stream(aws_credentials_file);
    aws_credentials_file_stream << "accessKey=key" << std::endl;
    aws_credentials_file_stream << "secretKey=secret" << std::endl;
    aws_credentials_file_stream.close();
    plan->setProperty(component, "Credentials File", aws_credentials_file);
  }

  template<typename Component>
  void setUseDefaultCredentialsChain(const Component &component) {
    #ifdef WIN32
    _putenv_s("AWS_ACCESS_KEY_ID", "key");
    _putenv_s("AWS_SECRET_ACCESS_KEY", "secret");
    #else
    setenv("AWS_ACCESS_KEY_ID", "key", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "secret", 1);
    #endif
    plan->setProperty(component, "Use Default Credentials", "true");
  }

  void setCredentialsService() {
    plan->setProperty(s3_processor, "AWS Credentials Provider service", "AWSCredentialsService");
  }

  void setBucket() {
    plan->setProperty(update_attribute, "test.bucket", S3_BUCKET, true);
    plan->setProperty(s3_processor, "Bucket", "${test.bucket}");
  }

  void setRequiredProperties() {
    setAccesKeyCredentialsInProcessor();
    setBucket();
  }

  void setProxy() {
    plan->setProperty(update_attribute, "test.proxyHost", "host", true);
    plan->setProperty(s3_processor, "Proxy Host", "${test.proxyHost}");
    plan->setProperty(update_attribute, "test.proxyPort", "1234", true);
    plan->setProperty(s3_processor, "Proxy Port", "${test.proxyPort}");
    plan->setProperty(update_attribute, "test.proxyUsername", "username", true);
    plan->setProperty(s3_processor, "Proxy Username", "${test.proxyUsername}");
    plan->setProperty(update_attribute, "test.proxyPassword", "password", true);
    plan->setProperty(s3_processor, "Proxy Password", "${test.proxyPassword}");
  }

  void checkProxySettings() {
    REQUIRE(mock_s3_wrapper_ptr->getClientConfig().proxyHost == "host");
    REQUIRE(mock_s3_wrapper_ptr->getClientConfig().proxyPort == 1234);
    REQUIRE(mock_s3_wrapper_ptr->getClientConfig().proxyUserName == "username");
    REQUIRE(mock_s3_wrapper_ptr->getClientConfig().proxyPassword == "password");
  }

  virtual ~S3TestsFixture() {
    LogTestController::getInstance().reset();
  }

 protected:
  TestController test_controller;
  std::shared_ptr<TestPlan> plan;
  MockS3Wrapper* mock_s3_wrapper_ptr;
  std::shared_ptr<core::Processor> s3_processor;
  std::shared_ptr<core::Processor> get_file;
  std::shared_ptr<core::Processor> update_attribute;
  std::shared_ptr<core::controller::ControllerServiceNode> aws_credentials_service;
};
