#ifndef INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_HUOBI_COIN_SWAP_H_
#define INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_HUOBI_COIN_SWAP_H_
#ifdef CCAPI_ENABLE_SERVICE_MARKET_DATA
#ifdef CCAPI_ENABLE_EXCHANGE_HUOBI_COIN_SWAP
#include "ccapi_cpp/service/ccapi_market_data_service_huobi_base.h"
namespace ccapi {
class MarketDataServiceHuobiCoinSwap : public MarketDataServiceHuobiBase {
 public:
  MarketDataServiceHuobiCoinSwap(std::function<void(Event& event)> wsEventHandler, SessionOptions sessionOptions, SessionConfigs sessionConfigs,
                                 std::shared_ptr<ServiceContext> serviceContextPtr)
      : MarketDataServiceHuobiBase(wsEventHandler, sessionOptions, sessionConfigs, serviceContextPtr) {
    this->exchangeName = CCAPI_EXCHANGE_NAME_HUOBI_COIN_SWAP;
    this->baseUrl = sessionConfigs.getUrlWebsocketBase().at(this->exchangeName) + "/swap-ws";
    this->isDerivatives = true;
    this->baseUrlRest = this->sessionConfigs.getUrlRestBase().at(this->exchangeName);
    this->setHostRestFromUrlRest(this->baseUrlRest);
    try {
      this->tcpResolverResultsRest = this->resolver.resolve(this->hostRest, this->portRest);
    } catch (const std::exception& e) {
      CCAPI_LOGGER_FATAL(std::string("e.what() = ") + e.what());
    }
    this->getRecentTradesTarget = CCAPI_HUOBI_COIN_SWAP_GET_RECENT_TRADES_PATH;
  }
  virtual ~MarketDataServiceHuobiCoinSwap() {}
};
} /* namespace ccapi */
#endif
#endif
#endif  // INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_HUOBI_COIN_SWAP_H_
