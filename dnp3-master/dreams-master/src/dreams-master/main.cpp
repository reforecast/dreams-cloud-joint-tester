/*
 * Copyright 2013-2019 Automatak, LLC
 *
 * Licensed to Green Energy Corp (www.greenenergycorp.com) and Automatak
 * LLC (www.automatak.com) under one or more contributor license agreements.
 * See the NOTICE file distributed with this work for additional information
 * regarding copyright ownership. Green Energy Corp and Automatak LLC license
 * this file to you under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License. You may obtain
 * a copy of the License at:
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <asiopal/UTCTimeSource.h>

#include <opendnp3/LogLevels.h>

#include "DreamsSOEHandler.hpp"
#include "Gateway.hpp"
#include "MesgBuffer.hpp"
#include "Plant.hpp"
#include "dreams.hpp"
#include "utils.hpp"
#include <asiodnp3/ConsoleLogger.h>
#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/DefaultMasterApplication.h>
#include <asiodnp3/PrintingChannelListener.h>
#include <asiodnp3/PrintingCommandCallback.h>

#include <sstream>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <sys/ipc.h>
#include <sys/msg.h>

#include <curl/curl.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#define MAX_MASTER_NUM 1000

using namespace std;
using namespace openpal;
using namespace asiopal;
using namespace asiodnp3;
using namespace opendnp3;
using namespace rapidjson;
using namespace dreams;

string construct_url();

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  ((std::string *)userp)->append((char *)contents, size * nmemb);
  return size * nmemb;
}

Value& json_traverse(Value& obj, const char* path) {
  string part;
  stringstream iss(path);
  Value* cursor = &obj;
  while(getline(iss, part, '.')) {
    if (cursor->IsArray() && part[0] == '[' && part[part.size() - 1] == ']') { //array
      int pos = stoi(part.substr(1, part.size() - 2));
      cursor = &((*cursor)[pos]);
    } else if (cursor->IsObject()) {
      cursor = &((*cursor)[part.c_str()]);
    } else {
      break;
    }
  }
  return (*cursor);
}

int json_get(Value& obj, const char* path, const int fallback) {
  Value& value = json_traverse(obj, path);
  return value.IsInt() ? value.GetInt() : fallback;
}

string json_get(Value& obj, const char* path, const string fallback) {
  Value& value = json_traverse(obj, path);
  return value.IsString() ? value.GetString() : fallback;
}

double json_get(Value& obj, const char* path, const double fallback) {
  Value& value = json_traverse(obj, path);
  return (value.IsDouble() || value.IsInt()) ? value.GetDouble() : fallback;
}

int main(int, char **) {
  // This is the main point of interaction with the stack
  DNP3Manager manager(1, ConsoleLogger::Create());
  map<string, shared_ptr<Gateway>> gateways;

  // Get all gateway IP address by using curl
  CURL *curl;
  std::string readBuffer;

  curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    string url = construct_url();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    Document document;
    document.Parse(readBuffer.c_str());
    assert(document.IsArray());
    for (SizeType i = 0; i < document.Size(); i++) { // Uses SizeType instead of size_t
      assert(document[i].IsObject());
      std::string plantNo = json_get(document[i], "plantNo", "");
      std::string plantName = json_get(document[i], "plantName", "");
      std::string plantCategory = json_get(document[i], "plantCategory", "");
      std::string gatewayAddress = json_get(document[i], "gateway.ipAddress", "");
      uint16_t gatewayPort = json_get(document[i], "gateway.port", 20000);
      double ctRatio = json_get(document[i], "gateway.powerMeterCTRatio", 1.0);
      double ptRatio = json_get(document[i], "gateway.powerMeterPTRatio", 1.0);
      uint16_t remoteAddr = json_get(document[i], "dnp3Address", 4);


      if (gateways.find(gatewayAddress) == gateways.end()) {
        auto gateway = make_shared<Gateway>(&manager, gatewayAddress, gatewayPort);
        gateways[gatewayAddress] = gateway;
      }

      if (plantCategory == "energyStorage") {
        std::shared_ptr<DreamsPoints> essDreamsPoints = std::make_shared<DreamsEnergyStoragePoints>();
        gateways[gatewayAddress]->AddMaster(remoteAddr, plantNo, plantName, ctRatio, ptRatio, essDreamsPoints);
      }
      else {
        std::shared_ptr<DreamsPoints> gridDreamsPoints = std::make_shared<DreamsGridPoints>();
        gateways[gatewayAddress]->AddMaster(remoteAddr, plantNo, plantName, ctRatio, ptRatio, gridDreamsPoints);
      }

      cout << "Gateway added for plant: " << plantNo << ", " << plantName << ", " << gatewayAddress << ", " << gatewayPort << ", " <<
        ctRatio << ", " << ptRatio << ", " << remoteAddr << endl;
    }
  }

  key_t key;
  int msgid;
  MesgBuffer message;

  // ftok to generate unique key
  key = ftok("progfile", 65);

  // msgget creates a message queue and returns identifier
  msgid = msgget(key, 0666 | IPC_CREAT);
  // clear messages and destroy the message queue
  msgctl(msgid, IPC_RMID, NULL);

  // create a message queue again
  msgid = msgget(key, 0666 | IPC_CREAT);

  // while loop for reading ipc message queue
  do {
    msgrcv(msgid, &message, sizeof(message), 0, 0);
    if (message.mesg_type == MESG_TYPE_QUIT_PROGRAM) {
      break;
    }

    std::string readedGatewayAddress = message.gateway_address;
    std::map<uint32_t, dreams::Plant>::iterator itr = gateways[readedGatewayAddress]->plants.find(message.dnp3_address);
    if (itr != gateways[readedGatewayAddress]->plants.end()) {
      auto &plant = itr->second;
      auto master = plant.master;
      if (message.mesg_type == MESG_TYPE_INTEGRITY_POLL) {
        master->ScanClasses(ClassField::AllClasses());
        printf("** Manually perform integrity poll\n");
        continue;
      }

      master->PerformFunction("direct operate", FunctionCode::DIRECT_OPERATE, {Header::AllObjects(41, 2)});
      printf("** PerformFunction AllObjects(41, 2) \n");
      master->SelectAndOperate(AnalogOutputInt16((int16_t)message.value), message.index,
                               PrintingCommandCallback::Get());
      printf("[%s %s] Write value=%d to analog output index=%d\n", readedGatewayAddress.c_str(), plant.plantNo.c_str(),
             (int16_t)message.value, message.index);
    }

  } while (message.mesg_type != MESG_TYPE_QUIT_PROGRAM);

  // to destroy the message queue
  msgctl(msgid, IPC_RMID, NULL);

  return 0;
}

string construct_url() {
  stringstream ss;
  string host = getenv_default("API_HOST", "localhost");
  string port_str = getenv_default("API_PORT", "3000");
  unsigned short port;
  istringstream(port_str) >> port;
  string path = "/api/Plants";
  string token = getenv_default("ADMIN_ACCESS_TOKEN", "");
  Document filter;
  Document::AllocatorType &allocator = filter.GetAllocator();
  filter.SetObject();

  Value fields;
  fields.SetArray();
  fields.PushBack("id", allocator);
  fields.PushBack("plantNo", allocator);
  fields.PushBack("plantName", allocator);
  fields.PushBack("gatewayId", allocator);
  fields.PushBack("dnp3Address", allocator);
  fields.PushBack("plantCategory", allocator);

  Value include;
  include.SetObject();
  include.AddMember("relation", "gateway", allocator);

  Value scope;
  scope.SetObject();
  Value scope_fields;
  scope_fields.SetArray();
  scope_fields.PushBack("ipAddress", allocator);
  scope_fields.PushBack("port", allocator);
  scope_fields.PushBack("powerMeterCTRatio", allocator);
  scope_fields.PushBack("powerMeterPTRatio", allocator);
  scope.AddMember("fields", scope_fields, allocator);
  include.AddMember("scope", scope, allocator);

  filter.AddMember("fields", fields, allocator);
  filter.AddMember("include", include, allocator);
  StringBuffer buffer;
  Writer<StringBuffer> writer(buffer);
  filter.Accept(writer);
  const char *filter_string = buffer.GetString();

  ss << "http://" << host << ":" << port << path << "?"
     << "access_token=" << token << "&filter=" << filter_string;
  return ss.str();
}
