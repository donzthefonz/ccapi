#ifndef INCLUDE_CCAPI_CPP_CCAPI_SUBSCRIPTION_H_
#define INCLUDE_CCAPI_CPP_CCAPI_SUBSCRIPTION_H_
#include <set>
#include <string>
#include "ccapi_cpp/ccapi_macro.h"
#include "ccapi_cpp/ccapi_util_private.h"
namespace ccapi {
class Subscription CCAPI_FINAL {
 public:
  Subscription() {}
  // breaking change to be added for next major version
  // Subscription(std::string exchange, std::string instrument, std::string field, std::map<std::string, std::string> optionMap = {}, std::string correlationId
  // = "",
  //              std::map<std::string, std::string> credential = {})
  //     : exchange(exchange), instrument(instrument), field(field), optionMap(optionMap),correlationId(correlationId), credential(credential) {
  //       std::map<std::string, std::string> optionMapDefault = {
  //         {CCAPI_MARKET_DEPTH_MAX,CCAPI_MARKET_DEPTH_MAX_DEFAULT},
  //         {CCAPI_CONFLATE_INTERVAL_MILLISECONDS,CCAPI_CONFLATE_INTERVAL_MILLISECONDS_DEFAULT},
  //         {CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS,CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS_DEFAULT},
  //         {CCAPI_MARKET_DEPTH_RETURN_UPDATE,CCAPI_MARKET_DEPTH_RETURN_UPDATE_DEFAULT},
  //       };
  //       for (const auto& x: optionMapDefault){
  //         if (this->optionMap.find(x.first)==this->optionMap.end()){
  //           this->optionMap.insert({x.first, x.second});
  //         }
  //       }
  //   if (field == CCAPI_FIX) {
  //     this->serviceName = CCAPI_FIX;
  //   } else if (field == CCAPI_EM_ORDER_UPDATE || field == CCAPI_EM_PRIVATE_TRADE) {
  //     this->serviceName = CCAPI_EXECUTION_MANAGEMENT;
  //   } else if (field == CCAPI_MARKET_DEPTH || field == CCAPI_TRADE) {
  //     this->serviceName = CCAPI_MARKET_DATA;
  //   }
  //   CCAPI_LOGGER_TRACE("this->serviceName = " + this->serviceName);
  //   if (this->correlationId.empty()) {
  //     this->correlationId = UtilString::generateRandomString(CCAPI_CORRELATION_ID_GENERATED_LENGTH);
  //   }
  //   this->instrumentSet = UtilString::splitToSet(instrument, ",");
  //   this->fieldSet = UtilString::splitToSet(field, ",");
  // }
  Subscription(std::string exchange, std::string instrument, std::string field, std::string options = "", std::string correlationId = "",
               std::map<std::string, std::string> credential = {})
      : exchange(exchange), instrument(instrument), field(field), correlationId(correlationId), credential(credential) {
    std::vector<std::string> optionList;
    if (!options.empty()) {
      optionList = UtilString::split(options, "&");
    }
    this->optionMap[CCAPI_MARKET_DEPTH_MAX] = CCAPI_MARKET_DEPTH_MAX_DEFAULT;
    this->optionMap[CCAPI_CONFLATE_INTERVAL_MILLISECONDS] = CCAPI_CONFLATE_INTERVAL_MILLISECONDS_DEFAULT;
    this->optionMap[CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS] = CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS_DEFAULT;
    this->optionMap[CCAPI_MARKET_DEPTH_RETURN_UPDATE] = CCAPI_MARKET_DEPTH_RETURN_UPDATE_DEFAULT;
    for (const auto& option : optionList) {
      auto optionKeyValue = UtilString::split(option, "=");
      this->optionMap[optionKeyValue.at(0)] = optionKeyValue.at(1);
    }
    if (field == CCAPI_FIX) {
      this->serviceName = CCAPI_FIX;
    } else if (field == CCAPI_EM_ORDER_UPDATE || field == CCAPI_EM_PRIVATE_TRADE) {
      this->serviceName = CCAPI_EXECUTION_MANAGEMENT;
    } else if (field == CCAPI_MARKET_DEPTH || field == CCAPI_TRADE) {
      this->serviceName = CCAPI_MARKET_DATA;
    }
    CCAPI_LOGGER_TRACE("this->serviceName = " + this->serviceName);
    if (this->correlationId.empty()) {
      this->correlationId = UtilString::generateRandomString(CCAPI_CORRELATION_ID_GENERATED_LENGTH);
    }
    this->instrumentSet = UtilString::splitToSet(instrument, ",");
    this->fieldSet = UtilString::splitToSet(field, ",");
  }
  std::string toString() const {
    std::map<std::string, std::string> shortCredential;
    for (const auto& x : credential) {
      shortCredential.insert(std::make_pair(x.first, UtilString::firstNCharacter(x.second, CCAPI_CREDENTIAL_DISPLAY_LENGTH)));
    }
    std::string output = "Subscription [exchange = " + exchange + ", instrument = " + instrument + ", field = " + field +
                         ", optionMap = " + ccapi::toString(optionMap) + ", correlationId = " + correlationId +
                         ", credential = " + ccapi::toString(shortCredential) + ", serviceName = " + serviceName + "]";
    return output;
  }
  const std::string& getCorrelationId() const { return correlationId; }
  const std::string& getExchange() const { return exchange; }
  const std::string& getInstrument() const { return instrument; }
  const std::string& getField() const { return field; }
  const std::map<std::string, std::string>& getOptionMap() const { return optionMap; }
  const std::map<std::string, std::string>& getCredential() const { return credential; }
  const std::string& getServiceName() const { return serviceName; }
  const std::set<std::string>& getInstrumentSet() const { return instrumentSet; }
  const std::set<std::string>& getFieldSet() const { return fieldSet; }
  const std::string getSerializedOptions() const {
    std::string output;
    int i = 0;
    for (const auto& option : this->optionMap) {
      output += option.first;
      output += "=";
      output += option.second;
      if (i < this->optionMap.size() - 1) {
        output += "&";
      }
      ++i;
    }
    return output;
  }
  enum class Status {
    UNKNOWN,
    SUBSCRIBING,
    SUBSCRIBED,
    UNSUBSCRIBING,
    UNSUBSCRIBED,
  };
  static std::string statusToString(Status status) {
    std::string output;
    switch (status) {
      case Status::UNKNOWN:
        output = "UNKNOWN";
        break;
      case Status::SUBSCRIBING:
        output = "SUBSCRIBING";
        break;
      case Status::SUBSCRIBED:
        output = "SUBSCRIBED";
        break;
      case Status::UNSUBSCRIBING:
        output = "UNSUBSCRIBING";
        break;
      case Status::UNSUBSCRIBED:
        output = "UNSUBSCRIBED";
        break;
      default:
        CCAPI_LOGGER_FATAL(CCAPI_UNSUPPORTED_VALUE);
    }
    return output;
  }
#ifndef CCAPI_EXPOSE_INTERNAL

 private:
#endif
  std::string exchange;
  std::string instrument;
  std::string field;
  std::map<std::string, std::string> optionMap;
  std::string correlationId;
  std::map<std::string, std::string> credential;
  std::string serviceName;
  std::set<std::string> instrumentSet;
  std::set<std::string> fieldSet;
};
} /* namespace ccapi */
#endif  // INCLUDE_CCAPI_CPP_CCAPI_SUBSCRIPTION_H_
