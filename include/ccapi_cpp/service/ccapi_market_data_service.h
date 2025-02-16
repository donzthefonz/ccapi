#ifndef INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_H_
#define INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_H_
#ifdef CCAPI_ENABLE_SERVICE_MARKET_DATA
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "ccapi_cpp/ccapi_decimal.h"
#include "ccapi_cpp/ccapi_logger.h"
#include "ccapi_cpp/ccapi_util_private.h"
#include "ccapi_cpp/service/ccapi_service.h"
namespace ccapi {
class MarketDataService : public Service {
 public:
  MarketDataService(std::function<void(Event& event)> eventHandler, SessionOptions sessionOptions, SessionConfigs sessionConfigs,
                    std::shared_ptr<ServiceContext> serviceContextPtr)
      : Service(eventHandler, sessionOptions, sessionConfigs, serviceContextPtr) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    this->requestOperationToMessageTypeMap = {
        {Request::Operation::GET_RECENT_TRADES, Message::Type::GET_RECENT_TRADES},
    };
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  virtual ~MarketDataService() {
    for (const auto& x : this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap) {
      for (const auto& y : x.second) {
        for (const auto& z : y.second) {
          z.second->cancel();
        }
      }
    }
  }
  // subscriptions are grouped and each group creates a unique websocket connection
  void subscribe(const std::vector<Subscription>& subscriptionList) override {
    CCAPI_LOGGER_FUNCTION_ENTER;
    CCAPI_LOGGER_DEBUG("this->baseUrl = " + this->baseUrl);
    if (this->shouldContinue.load()) {
      for (const auto& x : this->groupSubscriptionListByInstrumentGroup(subscriptionList)) {
        auto instrumentGroup = x.first;
        auto subscriptionListGivenInstrumentGroup = x.second;
        wspp::lib::asio::post(this->serviceContextPtr->tlsClientPtr->get_io_service(), [that = shared_from_base<MarketDataService>(), instrumentGroup,
                                                                                        subscriptionListGivenInstrumentGroup]() {
          std::map<std::string, std::vector<std::string>> wsConnectionIdListByInstrumentGroupMap = invertMapMulti(that->instrumentGroupByWsConnectionIdMap);
          if (wsConnectionIdListByInstrumentGroupMap.find(instrumentGroup) != wsConnectionIdListByInstrumentGroupMap.end() &&
              that->subscriptionStatusByInstrumentGroupInstrumentMap.find(instrumentGroup) != that->subscriptionStatusByInstrumentGroupInstrumentMap.end()) {
            auto wsConnectionId = wsConnectionIdListByInstrumentGroupMap.at(instrumentGroup).at(0);
            auto wsConnection = that->wsConnectionByIdMap.at(wsConnectionId);
            for (const auto& subscription : subscriptionListGivenInstrumentGroup) {
              auto instrument = subscription.getInstrument();
              if (that->subscriptionStatusByInstrumentGroupInstrumentMap[instrumentGroup].find(instrument) !=
                  that->subscriptionStatusByInstrumentGroupInstrumentMap[instrumentGroup].end()) {
                that->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::SUBSCRIPTION_FAILURE, "already subscribed: " + toString(subscription));
                return;
              }
              wsConnection.subscriptionList.push_back(subscription);
              that->subscriptionStatusByInstrumentGroupInstrumentMap[instrumentGroup][instrument] = Subscription::Status::SUBSCRIBING;
              that->prepareSubscription(wsConnection, subscription);
            }
            CCAPI_LOGGER_INFO("about to subscribe to exchange");
            that->subscribeToExchange(wsConnection);
          } else {
            auto url = UtilString::split(instrumentGroup, "|").at(0);
            WsConnection wsConnection(url, instrumentGroup, subscriptionListGivenInstrumentGroup);
            that->prepareConnect(wsConnection);
          }
        });
      }
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
#ifndef CCAPI_EXPOSE_INTERNAL

 protected:
#endif
  typedef wspp::lib::error_code ErrorCode;
  typedef wspp::lib::function<void(ErrorCode const&)> TimerHandler;
  std::map<std::string, std::vector<Subscription>> groupSubscriptionListByInstrumentGroup(const std::vector<Subscription>& subscriptionList) {
    std::map<std::string, std::vector<Subscription>> groups;
    for (const auto& subscription : subscriptionList) {
      std::string instrumentGroup = this->getInstrumentGroup(subscription);
      groups[instrumentGroup].push_back(subscription);
    }
    return groups;
  }
  virtual std::string getInstrumentGroup(const Subscription& subscription) {
    return this->baseUrl + "|" + subscription.getField() + "|" + subscription.getSerializedOptions();
  }
  virtual void prepareSubscriptionDetail(std::string& channelId, const std::string& field, const WsConnection& wsConnection, const std::string& symbolId,
                                         const std::map<std::string, std::string> optionMap) {
    auto marketDepthRequested = std::stoi(optionMap.at(CCAPI_MARKET_DEPTH_MAX));
    CCAPI_LOGGER_TRACE("marketDepthRequested = " + toString(marketDepthRequested));
    if (field == CCAPI_MARKET_DEPTH) {
      if (this->exchangeName == CCAPI_EXCHANGE_NAME_KRAKEN || this->exchangeName == CCAPI_EXCHANGE_NAME_BITFINEX ||
          this->exchangeName == CCAPI_EXCHANGE_NAME_BINANCE_US || this->exchangeName == CCAPI_EXCHANGE_NAME_BINANCE ||
          this->exchangeName == CCAPI_EXCHANGE_NAME_BINANCE_FUTURES) {
        int marketDepthSubscribedToExchange = 1;
        marketDepthSubscribedToExchange = this->calculateMarketDepthSubscribedToExchange(
            marketDepthRequested, this->sessionConfigs.getWebsocketAvailableMarketDepth().at(this->exchangeName));
        channelId += std::string("?") + CCAPI_MARKET_DEPTH_SUBSCRIBED_TO_EXCHANGE + "=" + std::to_string(marketDepthSubscribedToExchange);
        this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = marketDepthSubscribedToExchange;
      } else if (this->exchangeName == CCAPI_EXCHANGE_NAME_GEMINI) {
        if (marketDepthRequested == 1) {
          int marketDepthSubscribedToExchange = 1;
          channelId += std::string("?") + CCAPI_MARKET_DEPTH_SUBSCRIBED_TO_EXCHANGE + "=" + std::to_string(marketDepthSubscribedToExchange);
          this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = marketDepthSubscribedToExchange;
        }
      } else if (this->exchangeName == CCAPI_EXCHANGE_NAME_BITMEX) {
        if (marketDepthRequested == 1) {
          channelId = CCAPI_WEBSOCKET_BITMEX_CHANNEL_QUOTE;
        } else if (marketDepthRequested <= 10) {
          channelId = CCAPI_WEBSOCKET_BITMEX_CHANNEL_ORDER_BOOK_10;
        } else if (marketDepthRequested <= 25) {
          channelId = CCAPI_WEBSOCKET_BITMEX_CHANNEL_ORDER_BOOK_L2_25;
        }
      } else if (this->exchangeName == CCAPI_EXCHANGE_NAME_ERISX) {
        if (marketDepthRequested <= 20) {
          channelId = std::string(CCAPI_WEBSOCKET_ERISX_CHANNEL_TOP_OF_BOOK_MARKET_DATA_SUBSCRIBE) + "?" + CCAPI_MARKET_DEPTH_SUBSCRIBED_TO_EXCHANGE + "=" +
                      std::to_string(marketDepthRequested);
          this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = marketDepthRequested;
        } else {
          channelId += "|" + field;
        }
      } else if (this->exchangeName == CCAPI_EXCHANGE_NAME_KUCOIN) {
        if (marketDepthRequested == 1) {
          channelId = CCAPI_WEBSOCKET_KUCOIN_CHANNEL_MARKET_TICKER;
        } else if (marketDepthRequested <= 5) {
          channelId = CCAPI_WEBSOCKET_KUCOIN_CHANNEL_MARKET_LEVEL2DEPTH5;
        } else {
          channelId = CCAPI_WEBSOCKET_KUCOIN_CHANNEL_MARKET_LEVEL2DEPTH50;
        }
      } else if (this->exchangeName == CCAPI_EXCHANGE_NAME_FTX) {
        this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = 100;
      }
    } else if (field == CCAPI_TRADE) {
      if (this->exchangeName == CCAPI_EXCHANGE_NAME_ERISX) {
        channelId += "|" + field;
      }
    }
  }
  void prepareSubscription(const WsConnection& wsConnection, const Subscription& subscription) {
    auto instrument = subscription.getInstrument();
    CCAPI_LOGGER_TRACE("instrument = " + instrument);
    auto symbolId = this->convertInstrumentToWebsocketSymbolId(instrument);
    CCAPI_LOGGER_TRACE("symbolId = " + symbolId);
    auto field = subscription.getField();
    CCAPI_LOGGER_TRACE("field = " + field);
    auto optionMap = subscription.getOptionMap();
    CCAPI_LOGGER_TRACE("optionMap = " + toString(optionMap));
    std::string channelId = this->sessionConfigs.getExchangeFieldWebsocketChannelMap().at(this->exchangeName).at(field);
    CCAPI_LOGGER_TRACE("channelId = " + channelId);
    CCAPI_LOGGER_TRACE("this->exchangeName = " + this->exchangeName);
    this->prepareSubscriptionDetail(channelId, field, wsConnection, symbolId, optionMap);
    CCAPI_LOGGER_TRACE("channelId = " + channelId);
    this->correlationIdListByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId].push_back(subscription.getCorrelationId());
    this->subscriptionListByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId].push_back(subscription);
    this->fieldByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = field;
    this->optionMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId].insert(optionMap.begin(), optionMap.end());
    CCAPI_LOGGER_TRACE("this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap = " +
                       toString(this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap));
    CCAPI_LOGGER_TRACE("this->correlationIdListByConnectionIdChannelSymbolIdMap = " + toString(this->correlationIdListByConnectionIdChannelIdSymbolIdMap));
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  void onTextMessage(wspp::connection_hdl hdl, const std::string& textMessage, const TimePoint& timeReceived) override {
    CCAPI_LOGGER_FUNCTION_ENTER;
    WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
    std::vector<MarketDataMessage> marketDataMessageList = this->processTextMessage(wsConnection, hdl, textMessage, timeReceived);
    CCAPI_LOGGER_TRACE("marketDataMessageList = " + toString(marketDataMessageList));
    if (!marketDataMessageList.empty()) {
      for (auto& marketDataMessage : marketDataMessageList) {
        // TODO(cryptochassis): should make Event outside of this for-loop, but need to carefully study the implications
        Event event;
        bool shouldEmitEvent = true;
        if (marketDataMessage.type == MarketDataMessage::Type::MARKET_DATA_EVENTS) {
          if (marketDataMessage.recapType == MarketDataMessage::RecapType::NONE && this->sessionOptions.warnLateEventMaxMilliSeconds > 0 &&
              std::chrono::duration_cast<std::chrono::milliseconds>(timeReceived - marketDataMessage.tp).count() >
                  this->sessionOptions.warnLateEventMaxMilliSeconds) {
            CCAPI_LOGGER_WARN("late websocket message: timeReceived = " + toString(timeReceived) +
                              ", marketDataMessage.tp = " + toString(marketDataMessage.tp) + ", wsConnection = " + toString(wsConnection));
          }
          event.setType(Event::Type::SUBSCRIPTION_DATA);
          std::string& exchangeSubscriptionId = marketDataMessage.exchangeSubscriptionId;
          std::string& channelId =
              this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap.at(wsConnection.id).at(exchangeSubscriptionId).at(CCAPI_CHANNEL_ID);
          std::string& symbolId =
              this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap.at(wsConnection.id).at(exchangeSubscriptionId).at(CCAPI_SYMBOL_ID);
          auto& field = this->fieldByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
          CCAPI_LOGGER_TRACE("this->optionMapByConnectionIdChannelIdSymbolIdMap = " + toString(this->optionMapByConnectionIdChannelIdSymbolIdMap));
          CCAPI_LOGGER_TRACE("wsConnection = " + toString(wsConnection));
          CCAPI_LOGGER_TRACE("channelId = " + toString(channelId));
          CCAPI_LOGGER_TRACE("symbolId = " + toString(symbolId));
          auto& optionMap = this->optionMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
          CCAPI_LOGGER_TRACE("optionMap = " + toString(optionMap));
          auto& correlationIdList = this->correlationIdListByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
          CCAPI_LOGGER_TRACE("correlationIdList = " + toString(correlationIdList));
          if (marketDataMessage.data.find(MarketDataMessage::DataType::BID) != marketDataMessage.data.end() ||
              marketDataMessage.data.find(MarketDataMessage::DataType::ASK) != marketDataMessage.data.end()) {
            std::map<Decimal, std::string>& snapshotBid = this->snapshotBidByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId];
            std::map<Decimal, std::string>& snapshotAsk = this->snapshotAskByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId];
            if (this->processedInitialSnapshotByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] &&
                marketDataMessage.recapType == MarketDataMessage::RecapType::NONE) {
              this->processOrderBookUpdate(wsConnection, channelId, symbolId, event, shouldEmitEvent, marketDataMessage.tp, timeReceived,
                                           marketDataMessage.data, field, optionMap, correlationIdList, snapshotBid, snapshotAsk);
              if (this->sessionOptions.enableCheckOrderBookChecksum &&
                  this->orderBookChecksumByConnectionIdSymbolIdMap.find(wsConnection.id) != this->orderBookChecksumByConnectionIdSymbolIdMap.end() &&
                  this->orderBookChecksumByConnectionIdSymbolIdMap.at(wsConnection.id).find(symbolId) !=
                      this->orderBookChecksumByConnectionIdSymbolIdMap.at(wsConnection.id).end()) {
                bool shouldProcessRemainingMessage = true;
                std::string receivedOrderBookChecksumStr = this->orderBookChecksumByConnectionIdSymbolIdMap[wsConnection.id][symbolId];
                if (!this->checkOrderBookChecksum(snapshotBid, snapshotAsk, receivedOrderBookChecksumStr, shouldProcessRemainingMessage)) {
                  CCAPI_LOGGER_ERROR("snapshotBid = " + toString(snapshotBid));
                  CCAPI_LOGGER_ERROR("snapshotAsk = " + toString(snapshotAsk));
                  this->onIncorrectStatesFound(wsConnection, hdl, textMessage, timeReceived, exchangeSubscriptionId, "order book incorrect checksum found");
                }
                if (!shouldProcessRemainingMessage) {
                  return;
                }
              }
              if (this->sessionOptions.enableCheckOrderBookCrossed) {
                bool shouldProcessRemainingMessage = true;
                if (!this->checkOrderBookCrossed(snapshotBid, snapshotAsk, shouldProcessRemainingMessage)) {
                  CCAPI_LOGGER_ERROR("lastNToString(snapshotBid, 1) = " + lastNToString(snapshotBid, 1));
                  CCAPI_LOGGER_ERROR("firstNToString(snapshotAsk, 1) = " + firstNToString(snapshotAsk, 1));
                  this->onIncorrectStatesFound(wsConnection, hdl, textMessage, timeReceived, exchangeSubscriptionId, "order book crossed market found");
                }
                if (!shouldProcessRemainingMessage) {
                  return;
                }
              }
            } else if (marketDataMessage.recapType == MarketDataMessage::RecapType::SOLICITED) {
              this->processOrderBookInitial(wsConnection, channelId, symbolId, event, shouldEmitEvent, marketDataMessage.tp, timeReceived,
                                            marketDataMessage.data, field, optionMap, correlationIdList, snapshotBid, snapshotAsk);
            }
            CCAPI_LOGGER_TRACE("snapshotBid.size() = " + toString(snapshotBid.size()));
            CCAPI_LOGGER_TRACE("snapshotAsk.size() = " + toString(snapshotAsk.size()));
          }
          if (marketDataMessage.data.find(MarketDataMessage::DataType::TRADE) != marketDataMessage.data.end()) {
            this->processTrade(wsConnection, channelId, symbolId, event, shouldEmitEvent, marketDataMessage.tp, timeReceived, marketDataMessage.data, field,
                               optionMap, correlationIdList);
          }
        } else {
          CCAPI_LOGGER_WARN("websocket event type is unknown!");
        }
        CCAPI_LOGGER_TRACE("event type is " + event.typeToString(event.getType()));
        if (event.getType() == Event::Type::UNKNOWN) {
          CCAPI_LOGGER_WARN("event type is unknown!");
        } else {
          if (event.getMessageList().empty()) {
            CCAPI_LOGGER_DEBUG("event has no messages!");
            shouldEmitEvent = false;
          }
          if (shouldEmitEvent) {
            this->eventHandler(event);
          }
        }
      }
    }
    this->onPongByMethod(PingPongMethod::WEBSOCKET_APPLICATION_LEVEL, hdl, textMessage, timeReceived);
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  void updateOrderBook(std::map<Decimal, std::string>& snapshot, Decimal& price, std::string& size, bool sizeMayHaveTrailingZero = false) {
    auto it = snapshot.find(price);
    if (it == snapshot.end()) {
      if ((!sizeMayHaveTrailingZero && size != "0") ||
          (sizeMayHaveTrailingZero && size.find('.') != std::string::npos && UtilString::rtrim(UtilString::rtrim(size, "0"), ".") != "0")) {
        snapshot.emplace(std::move(price), std::move(size));
      }
    } else {
      if ((!sizeMayHaveTrailingZero && size != "0") ||
          (sizeMayHaveTrailingZero && size.find('.') != std::string::npos && UtilString::rtrim(UtilString::rtrim(size, "0"), ".") != "0")) {
        it->second = std::move(size);
      } else {
        snapshot.erase(price);
      }
    }
  }
  void updateElementListWithInitialMarketDepth(const std::string& field, const std::map<std::string, std::string>& optionMap,
                                               const std::map<Decimal, std::string>& snapshotBid, const std::map<Decimal, std::string>& snapshotAsk,
                                               std::vector<Element>& elementList) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    if (field == CCAPI_MARKET_DEPTH) {
      int maxMarketDepth = std::stoi(optionMap.at(CCAPI_MARKET_DEPTH_MAX));
      int bidIndex = 0;
      for (auto iter = snapshotBid.rbegin(); iter != snapshotBid.rend(); iter++) {
        if (bidIndex < maxMarketDepth) {
          Element element;
          element.insert(CCAPI_BEST_BID_N_PRICE, iter->first.toString());
          element.insert(CCAPI_BEST_BID_N_SIZE, iter->second);
          elementList.push_back(std::move(element));
        }
        ++bidIndex;
      }
      if (snapshotBid.empty()) {
        Element element;
        element.insert(CCAPI_BEST_BID_N_PRICE, CCAPI_BEST_BID_N_PRICE_EMPTY);
        element.insert(CCAPI_BEST_BID_N_SIZE, CCAPI_BEST_BID_N_SIZE_EMPTY);
        elementList.push_back(std::move(element));
      }
      int askIndex = 0;
      for (auto iter = snapshotAsk.begin(); iter != snapshotAsk.end(); iter++) {
        if (askIndex < maxMarketDepth) {
          Element element;
          element.insert(CCAPI_BEST_ASK_N_PRICE, iter->first.toString());
          element.insert(CCAPI_BEST_ASK_N_SIZE, iter->second);
          elementList.push_back(std::move(element));
        }
        ++askIndex;
      }
      if (snapshotAsk.empty()) {
        Element element;
        element.insert(CCAPI_BEST_ASK_N_PRICE, CCAPI_BEST_ASK_N_PRICE_EMPTY);
        element.insert(CCAPI_BEST_ASK_N_SIZE, CCAPI_BEST_ASK_N_SIZE_EMPTY);
        elementList.push_back(std::move(element));
      }
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  std::map<Decimal, std::string> calculateMarketDepthUpdate(bool isBid, const std::map<Decimal, std::string>& c1, const std::map<Decimal, std::string>& c2,
                                                            int maxMarketDepth) {
    if (c1.empty()) {
      std::map<Decimal, std::string> output;
      for (const auto& x : c2) {
        output.insert(std::make_pair(x.first, "0"));
      }
      return output;
    } else if (c2.empty()) {
      return c1;
    }
    if (isBid) {
      auto it1 = c1.rbegin();
      int i1 = 0;
      auto it2 = c2.rbegin();
      int i2 = 0;
      std::map<Decimal, std::string> output;
      while (i1 < maxMarketDepth && i2 < maxMarketDepth && it1 != c1.rend() && it2 != c2.rend()) {
        if (it1->first < it2->first) {
          output.insert(std::make_pair(it1->first, it1->second));
          ++it1;
          ++i1;
        } else if (it1->first > it2->first) {
          output.insert(std::make_pair(it2->first, "0"));
          ++it2;
          ++i2;
        } else {
          if (it1->second != it2->second) {
            output.insert(std::make_pair(it1->first, it1->second));
          }
          ++it1;
          ++i1;
          ++it2;
          ++i2;
        }
      }
      while (i1 < maxMarketDepth && it1 != c1.rend()) {
        output.insert(std::make_pair(it1->first, it1->second));
        ++it1;
        ++i1;
      }
      while (i2 < maxMarketDepth && it2 != c2.rend()) {
        output.insert(std::make_pair(it2->first, "0"));
        ++it2;
        ++i2;
      }
      return output;
    } else {
      auto it1 = c1.begin();
      int i1 = 0;
      auto it2 = c2.begin();
      int i2 = 0;
      std::map<Decimal, std::string> output;
      while (i1 < maxMarketDepth && i2 < maxMarketDepth && it1 != c1.end() && it2 != c2.end()) {
        if (it1->first < it2->first) {
          output.insert(std::make_pair(it1->first, it1->second));
          ++it1;
          ++i1;
        } else if (it1->first > it2->first) {
          output.insert(std::make_pair(it2->first, "0"));
          ++it2;
          ++i2;
        } else {
          if (it1->second != it2->second) {
            output.insert(std::make_pair(it1->first, it1->second));
          }
          ++it1;
          ++i1;
          ++it2;
          ++i2;
        }
      }
      while (i1 < maxMarketDepth && it1 != c1.end()) {
        output.insert(std::make_pair(it1->first, it1->second));
        ++it1;
        ++i1;
      }
      while (i2 < maxMarketDepth && it2 != c2.end()) {
        output.insert(std::make_pair(it2->first, "0"));
        ++it2;
        ++i2;
      }
      return output;
    }
  }
  void updateElementListWithUpdateMarketDepth(const std::string& field, const std::map<std::string, std::string>& optionMap,
                                              const std::map<Decimal, std::string>& snapshotBid, const std::map<Decimal, std::string>& snapshotBidPrevious,
                                              const std::map<Decimal, std::string>& snapshotAsk, const std::map<Decimal, std::string>& snapshotAskPrevious,
                                              std::vector<Element>& elementList, bool alwaysUpdate) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    if (field == CCAPI_MARKET_DEPTH) {
      int maxMarketDepth = std::stoi(optionMap.at(CCAPI_MARKET_DEPTH_MAX));
      if (optionMap.at(CCAPI_MARKET_DEPTH_RETURN_UPDATE) == CCAPI_MARKET_DEPTH_RETURN_UPDATE_ENABLE) {
        CCAPI_LOGGER_TRACE("lastNSame = " + toString(lastNSame(snapshotBid, snapshotBidPrevious, maxMarketDepth)));
        CCAPI_LOGGER_TRACE("firstNSame = " + toString(firstNSame(snapshotAsk, snapshotAskPrevious, maxMarketDepth)));
        std::map<Decimal, std::string> snapshotBidUpdate = this->calculateMarketDepthUpdate(true, snapshotBid, snapshotBidPrevious, maxMarketDepth);
        for (auto& x : snapshotBidUpdate) {
          Element element;
          std::string k1(CCAPI_BEST_BID_N_PRICE);
          std::string v1 = x.first.toString();
          element.emplace(k1, v1);
          std::string k2(CCAPI_BEST_BID_N_SIZE);
          element.emplace(k2, x.second);
          elementList.push_back(std::move(element));
        }
        std::map<Decimal, std::string> snapshotAskUpdate = this->calculateMarketDepthUpdate(false, snapshotAsk, snapshotAskPrevious, maxMarketDepth);
        for (auto& x : snapshotAskUpdate) {
          Element element;
          std::string k1(CCAPI_BEST_ASK_N_PRICE);
          std::string v1 = x.first.toString();
          element.emplace(k1, v1);
          std::string k2(CCAPI_BEST_ASK_N_SIZE);
          element.emplace(k2, x.second);
          elementList.push_back(std::move(element));
        }
      } else {
        CCAPI_LOGGER_TRACE("lastNSame = " + toString(lastNSame(snapshotBid, snapshotBidPrevious, maxMarketDepth)));
        CCAPI_LOGGER_TRACE("firstNSame = " + toString(firstNSame(snapshotAsk, snapshotAskPrevious, maxMarketDepth)));
        if (alwaysUpdate || !lastNSame(snapshotBid, snapshotBidPrevious, maxMarketDepth) || !firstNSame(snapshotAsk, snapshotAskPrevious, maxMarketDepth)) {
          int bidIndex = 0;
          for (auto iter = snapshotBid.rbegin(); iter != snapshotBid.rend(); ++iter) {
            if (bidIndex >= maxMarketDepth) {
              break;
            }
            Element element;
            element.insert(CCAPI_BEST_BID_N_PRICE, iter->first.toString());
            element.insert(CCAPI_BEST_BID_N_SIZE, iter->second);
            elementList.push_back(std::move(element));
            ++bidIndex;
          }
          if (snapshotBid.empty()) {
            Element element;
            element.insert(CCAPI_BEST_BID_N_PRICE, CCAPI_BEST_BID_N_PRICE_EMPTY);
            element.insert(CCAPI_BEST_BID_N_SIZE, CCAPI_BEST_BID_N_SIZE_EMPTY);
            elementList.push_back(std::move(element));
          }
          int askIndex = 0;
          for (auto iter = snapshotAsk.begin(); iter != snapshotAsk.end(); ++iter) {
            if (askIndex >= maxMarketDepth) {
              break;
            }
            Element element;
            element.insert(CCAPI_BEST_ASK_N_PRICE, iter->first.toString());
            element.insert(CCAPI_BEST_ASK_N_SIZE, iter->second);
            elementList.push_back(std::move(element));
            ++askIndex;
          }
          if (snapshotAsk.empty()) {
            Element element;
            element.insert(CCAPI_BEST_ASK_N_PRICE, CCAPI_BEST_ASK_N_PRICE_EMPTY);
            element.insert(CCAPI_BEST_ASK_N_SIZE, CCAPI_BEST_ASK_N_SIZE_EMPTY);
            elementList.push_back(std::move(element));
          }
        }
      }
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  void updateElementListWithTrade(const std::string& field, MarketDataMessage::TypeForData& input, std::vector<Element>& elementList) {
    if (field == CCAPI_TRADE) {
      for (auto& x : input) {
        auto& type = x.first;
        auto& detail = x.second;
        if (type == MarketDataMessage::DataType::TRADE) {
          for (auto& y : detail) {
            auto& price = y.at(MarketDataMessage::DataFieldType::PRICE);
            auto& size = y.at(MarketDataMessage::DataFieldType::SIZE);
            Element element;
            std::string k1(CCAPI_LAST_PRICE);
            std::string k2(CCAPI_LAST_SIZE);
            element.emplace(k1, y.at(MarketDataMessage::DataFieldType::PRICE));
            element.emplace(k2, y.at(MarketDataMessage::DataFieldType::SIZE));
            {
              auto it = y.find(MarketDataMessage::DataFieldType::TRADE_ID);
              if (it != y.end()) {
                std::string k3(CCAPI_TRADE_ID);
                element.emplace(k3, it->second);
              }
            }
            std::string k4(CCAPI_IS_BUYER_MAKER);
            element.emplace(k4, y.at(MarketDataMessage::DataFieldType::IS_BUYER_MAKER));
            {
              auto it = y.find(MarketDataMessage::DataFieldType::SEQUENCE_NUMBER);
              if (it != y.end()) {
                std::string k5(CCAPI_SEQUENCE_NUMBER);
                element.emplace(k5, it->second);
              }
            }
            elementList.push_back(std::move(element));
          }
        } else {
          CCAPI_LOGGER_WARN("extra type " + MarketDataMessage::dataTypeToString(type));
        }
      }
    }
  }
  void updateElementListWithOhlc(const WsConnection& wsConnection, const std::string& channelId, const std::string& symbolId, const std::string& field,
                                 std::vector<Element>& elementList) {
    if (field == CCAPI_TRADE) {
      Element element;
      if (this->openByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId].empty()) {
        element.insert(CCAPI_OPEN, CCAPI_OHLC_EMPTY);
        element.insert(CCAPI_HIGH, CCAPI_OHLC_EMPTY);
        element.insert(CCAPI_LOW, CCAPI_OHLC_EMPTY);
        element.insert(CCAPI_CLOSE, CCAPI_OHLC_EMPTY);
      } else {
        element.insert(CCAPI_OPEN, this->openByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]);
        element.insert(CCAPI_HIGH, this->highByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId].toString());
        element.insert(CCAPI_LOW, this->lowByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId].toString());
        element.insert(CCAPI_CLOSE, this->closeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]);
      }
      elementList.push_back(std::move(element));
      this->openByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = "";
      this->highByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = Decimal();
      this->lowByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = Decimal();
      this->closeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = "";
    }
  }
  void copySnapshot(bool isBid, const std::map<Decimal, std::string>& original, std::map<Decimal, std::string>& copy, const int maxMarketDepth) {
    size_t nToCopy = std::min(original.size(), static_cast<size_t>(maxMarketDepth));
    if (isBid) {
      std::copy_n(original.rbegin(), nToCopy, std::inserter(copy, copy.end()));
    } else {
      std::copy_n(original.begin(), nToCopy, std::inserter(copy, copy.end()));
    }
  }
  void processOrderBookInitial(const WsConnection& wsConnection, const std::string& channelId, const std::string& symbolId, Event& event, bool& shouldEmitEvent,
                               const TimePoint& tp, const TimePoint& timeReceived, MarketDataMessage::TypeForData& input, const std::string& field,
                               const std::map<std::string, std::string>& optionMap, const std::vector<std::string>& correlationIdList,
                               std::map<Decimal, std::string>& snapshotBid, std::map<Decimal, std::string>& snapshotAsk) {
    snapshotBid.clear();
    snapshotAsk.clear();
    int maxMarketDepth = std::stoi(optionMap.at(CCAPI_MARKET_DEPTH_MAX));
    for (auto& x : input) {
      auto& type = x.first;
      auto& detail = x.second;
      if (type == MarketDataMessage::DataType::BID) {
        for (auto& y : detail) {
          auto& price = y.at(MarketDataMessage::DataFieldType::PRICE);
          auto& size = y.at(MarketDataMessage::DataFieldType::SIZE);
          Decimal decimalPrice(price, this->sessionOptions.enableCheckOrderBookChecksum);
          snapshotBid.emplace(std::move(decimalPrice), std::move(size));
        }
        CCAPI_LOGGER_TRACE("lastNToString(snapshotBid, " + toString(maxMarketDepth) + ") = " + lastNToString(snapshotBid, maxMarketDepth));
      } else if (type == MarketDataMessage::DataType::ASK) {
        for (auto& y : detail) {
          auto& price = y.at(MarketDataMessage::DataFieldType::PRICE);
          auto& size = y.at(MarketDataMessage::DataFieldType::SIZE);
          Decimal decimalPrice(price, this->sessionOptions.enableCheckOrderBookChecksum);
          snapshotAsk.emplace(std::move(decimalPrice), std::move(size));
        }
        CCAPI_LOGGER_TRACE("firstNToString(snapshotAsk, " + toString(maxMarketDepth) + ") = " + firstNToString(snapshotAsk, maxMarketDepth));
      } else {
        CCAPI_LOGGER_WARN("extra type " + MarketDataMessage::dataTypeToString(type));
      }
    }
    std::vector<Element> elementList;
    this->updateElementListWithInitialMarketDepth(field, optionMap, snapshotBid, snapshotAsk, elementList);
    if (!elementList.empty()) {
      Message message;
      message.setTimeReceived(timeReceived);
      message.setType(Message::Type::MARKET_DATA_EVENTS);
      message.setRecapType(Message::RecapType::SOLICITED);
      message.setTime(tp);
      message.setElementList(elementList);
      message.setCorrelationIdList(correlationIdList);
      std::vector<Message> newMessageList = {message};
      event.addMessages(newMessageList);
      CCAPI_LOGGER_TRACE("event.getMessageList() = " + toString(event.getMessageList()));
    }
    this->processedInitialSnapshotByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = true;
    bool shouldConflate = optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS) != CCAPI_CONFLATE_INTERVAL_MILLISECONDS_DEFAULT;
    if (shouldConflate) {
      this->copySnapshot(true, snapshotBid, this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId],
                         maxMarketDepth);
      this->copySnapshot(false, snapshotAsk, this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId],
                         maxMarketDepth);
      CCAPI_LOGGER_TRACE(
          "this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at("
          "symbolId) = " +
          toString(this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId)));
      CCAPI_LOGGER_TRACE(
          "this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at("
          "symbolId) = " +
          toString(this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId)));
      TimePoint previousConflateTp = UtilTime::makeTimePointFromMilliseconds(
          std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count() / std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)) *
          std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)));
      this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = previousConflateTp;
      if (optionMap.at(CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS) != CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS_DEFAULT) {
        auto interval = std::chrono::milliseconds(std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)));
        auto gracePeriod = std::chrono::milliseconds(std::stoi(optionMap.at(CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS)));
        this->setConflateTimer(previousConflateTp, interval, gracePeriod, wsConnection, channelId, symbolId, field, optionMap, correlationIdList);
      }
    }
  }
  void processOrderBookUpdate(const WsConnection& wsConnection, const std::string& channelId, const std::string& symbolId, Event& event, bool& shouldEmitEvent,
                              const TimePoint& tp, const TimePoint& timeReceived, MarketDataMessage::TypeForData& input, const std::string& field,
                              const std::map<std::string, std::string>& optionMap, const std::vector<std::string>& correlationIdList,
                              std::map<Decimal, std::string>& snapshotBid, std::map<Decimal, std::string>& snapshotAsk) {
    CCAPI_LOGGER_TRACE("input = " + MarketDataMessage::dataToString(input));
    if (this->processedInitialSnapshotByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]) {
      std::vector<Message> messageList;
      CCAPI_LOGGER_TRACE("optionMap = " + toString(optionMap));
      int maxMarketDepth = std::stoi(optionMap.at(CCAPI_MARKET_DEPTH_MAX));
      std::map<Decimal, std::string> snapshotBidPrevious;
      this->copySnapshot(true, snapshotBid, snapshotBidPrevious, maxMarketDepth);
      std::map<Decimal, std::string> snapshotAskPrevious;
      this->copySnapshot(false, snapshotAsk, snapshotAskPrevious, maxMarketDepth);
      CCAPI_LOGGER_TRACE("before updating orderbook");
      CCAPI_LOGGER_TRACE("lastNToString(snapshotBid, " + toString(maxMarketDepth) + ") = " + lastNToString(snapshotBid, maxMarketDepth));
      CCAPI_LOGGER_TRACE("firstNToString(snapshotAsk, " + toString(maxMarketDepth) + ") = " + firstNToString(snapshotAsk, maxMarketDepth));
      if (this->l2UpdateIsReplaceByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]) {
        CCAPI_LOGGER_TRACE("l2Update is replace");
        if (input.find(MarketDataMessage::DataType::BID) != input.end()) {
          snapshotBid.clear();
        }
        if (input.find(MarketDataMessage::DataType::ASK) != input.end()) {
          snapshotAsk.clear();
        }
      }
      for (auto& x : input) {
        auto& type = x.first;
        auto& detail = x.second;
        if (type == MarketDataMessage::DataType::BID) {
          for (auto& y : detail) {
            auto& price = y.at(MarketDataMessage::DataFieldType::PRICE);
            auto& size = y.at(MarketDataMessage::DataFieldType::SIZE);
            Decimal decimalPrice(price, this->sessionOptions.enableCheckOrderBookChecksum);
            this->updateOrderBook(snapshotBid, decimalPrice, size, this->sessionOptions.enableCheckOrderBookChecksum);
          }
        } else if (type == MarketDataMessage::DataType::ASK) {
          for (auto& y : detail) {
            auto& price = y.at(MarketDataMessage::DataFieldType::PRICE);
            auto& size = y.at(MarketDataMessage::DataFieldType::SIZE);
            Decimal decimalPrice(price, this->sessionOptions.enableCheckOrderBookChecksum);
            this->updateOrderBook(snapshotAsk, decimalPrice, size, this->sessionOptions.enableCheckOrderBookChecksum);
          }
        } else {
          CCAPI_LOGGER_WARN("extra type " + MarketDataMessage::dataTypeToString(type));
        }
      }
      CCAPI_LOGGER_TRACE("this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap = " +
                         toString(this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap));
      if (this->shouldAlignSnapshot) {
        int marketDepthSubscribedToExchange =
            this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
        this->alignSnapshot(snapshotBid, snapshotAsk, marketDepthSubscribedToExchange);
      }
      CCAPI_LOGGER_TRACE("afer updating orderbook");
      CCAPI_LOGGER_TRACE("lastNToString(snapshotBid, " + toString(maxMarketDepth) + ") = " + lastNToString(snapshotBid, maxMarketDepth));
      CCAPI_LOGGER_TRACE("firstNToString(snapshotAsk, " + toString(maxMarketDepth) + ") = " + firstNToString(snapshotAsk, maxMarketDepth));
      CCAPI_LOGGER_TRACE("lastNToString(snapshotBidPrevious, " + toString(maxMarketDepth) + ") = " + lastNToString(snapshotBidPrevious, maxMarketDepth));
      CCAPI_LOGGER_TRACE("firstNToString(snapshotAskPrevious, " + toString(maxMarketDepth) + ") = " + firstNToString(snapshotAskPrevious, maxMarketDepth));
      CCAPI_LOGGER_TRACE("field = " + toString(field));
      CCAPI_LOGGER_TRACE("maxMarketDepth = " + toString(maxMarketDepth));
      CCAPI_LOGGER_TRACE("optionMap = " + toString(optionMap));
      bool shouldConflate = optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS) != CCAPI_CONFLATE_INTERVAL_MILLISECONDS_DEFAULT;
      CCAPI_LOGGER_TRACE("shouldConflate = " + toString(shouldConflate));
      TimePoint conflateTp =
          shouldConflate ? UtilTime::makeTimePointFromMilliseconds(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count() /
                                                                   std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)) *
                                                                   std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)))
                         : tp;
      CCAPI_LOGGER_TRACE("conflateTp = " + toString(conflateTp));
      bool intervalChanged =
          shouldConflate && conflateTp > this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
      CCAPI_LOGGER_TRACE("intervalChanged = " + toString(intervalChanged));
      if (!shouldConflate || intervalChanged) {
        std::vector<Element> elementList;
        if (shouldConflate && intervalChanged) {
          const std::map<Decimal, std::string>& snapshotBidPreviousPrevious =
              this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
          const std::map<Decimal, std::string>& snapshotAskPreviousPrevious =
              this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
          this->updateElementListWithUpdateMarketDepth(field, optionMap, snapshotBidPrevious, snapshotBidPreviousPrevious, snapshotAskPrevious,
                                                       snapshotAskPreviousPrevious, elementList, false);
          this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId) = snapshotBidPrevious;
          this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId) = snapshotAskPrevious;
          CCAPI_LOGGER_TRACE(
              "this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at("
              "symbolId) = " +
              toString(this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId)));
          CCAPI_LOGGER_TRACE(
              "this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at("
              "symbolId) = " +
              toString(this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId)));
        } else {
          this->updateElementListWithUpdateMarketDepth(field, optionMap, snapshotBid, snapshotBidPrevious, snapshotAsk, snapshotAskPrevious, elementList,
                                                       false);
        }
        CCAPI_LOGGER_TRACE("elementList = " + toString(elementList));
        if (!elementList.empty()) {
          Message message;
          message.setTimeReceived(timeReceived);
          message.setType(Message::Type::MARKET_DATA_EVENTS);
          message.setRecapType(Message::RecapType::NONE);
          TimePoint time = shouldConflate ? this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId) +
                                                std::chrono::milliseconds(std::stoll(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)))
                                          : conflateTp;
          message.setTime(time);
          message.setElementList(elementList);
          message.setCorrelationIdList(correlationIdList);
          messageList.push_back(std::move(message));
        }
        if (!messageList.empty()) {
          event.addMessages(messageList);
        }
        if (shouldConflate) {
          this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId) = conflateTp;
        }
      }
    }
  }
  void processTrade(const WsConnection& wsConnection, const std::string& channelId, const std::string& symbolId, Event& event, bool& shouldEmitEvent,
                    const TimePoint& tp, const TimePoint& timeReceived, MarketDataMessage::TypeForData& input, const std::string& field,
                    const std::map<std::string, std::string>& optionMap, const std::vector<std::string>& correlationIdList) {
    CCAPI_LOGGER_TRACE("input = " + MarketDataMessage::dataToString(input));
    CCAPI_LOGGER_TRACE("optionMap = " + toString(optionMap));
    bool shouldConflate = optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS) != CCAPI_CONFLATE_INTERVAL_MILLISECONDS_DEFAULT;
    CCAPI_LOGGER_TRACE("shouldConflate = " + toString(shouldConflate));
    TimePoint conflateTp = shouldConflate
                               ? UtilTime::makeTimePointFromMilliseconds(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count() /
                                                                         std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)) *
                                                                         std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)))
                               : tp;
    CCAPI_LOGGER_TRACE("conflateTp = " + toString(conflateTp));
    if (!this->processedInitialTradeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]) {
      if (shouldConflate) {
        TimePoint previousConflateTp = conflateTp;
        this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = previousConflateTp;
        if (optionMap.at(CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS) != CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS_DEFAULT) {
          auto interval = std::chrono::milliseconds(std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)));
          auto gracePeriod = std::chrono::milliseconds(std::stoi(optionMap.at(CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS)));
          this->setConflateTimer(previousConflateTp, interval, gracePeriod, wsConnection, channelId, symbolId, field, optionMap, correlationIdList);
        }
      }
      this->processedInitialTradeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = true;
    }
    bool intervalChanged =
        shouldConflate && conflateTp > this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
    CCAPI_LOGGER_TRACE("intervalChanged = " + toString(intervalChanged));
    if (!shouldConflate || intervalChanged) {
      std::vector<Message> messageList;
      std::vector<Element> elementList;
      if (shouldConflate && intervalChanged) {
        this->updateElementListWithOhlc(wsConnection, channelId, symbolId, field, elementList);
      } else {
        this->updateElementListWithTrade(field, input, elementList);
      }
      CCAPI_LOGGER_TRACE("elementList = " + toString(elementList));
      if (!elementList.empty()) {
        Message message;
        message.setTimeReceived(timeReceived);
        message.setType(Message::Type::MARKET_DATA_EVENTS);
        message.setRecapType(Message::RecapType::NONE);
        TimePoint time =
            shouldConflate ? this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId) : conflateTp;
        message.setTime(time);
        message.setElementList(elementList);
        message.setCorrelationIdList(correlationIdList);
        messageList.push_back(std::move(message));
      }
      if (!messageList.empty()) {
        event.addMessages(messageList);
      }
      if (shouldConflate) {
        this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId) = conflateTp;
        this->updateOhlc(wsConnection, channelId, symbolId, field, input);
      }
    } else {
      this->updateOhlc(wsConnection, channelId, symbolId, field, input);
    }
  }
  void updateOhlc(const WsConnection& wsConnection, const std::string& channelId, const std::string& symbolId, const std::string& field,
                  const MarketDataMessage::TypeForData& input) {
    if (field == CCAPI_TRADE) {
      for (const auto& x : input) {
        auto type = x.first;
        auto detail = x.second;
        if (type == MarketDataMessage::DataType::TRADE) {
          for (const auto& y : detail) {
            auto price = y.at(MarketDataMessage::DataFieldType::PRICE);
            if (this->openByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId].empty()) {
              this->openByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = price;
              this->highByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = Decimal(price);
              this->lowByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = Decimal(price);
            } else {
              Decimal decimalPrice(price);
              if (decimalPrice > this->highByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]) {
                this->highByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = decimalPrice;
              }
              if (decimalPrice < this->lowByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]) {
                this->lowByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = decimalPrice;
              }
            }
            this->closeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = price;
          }
        } else {
          CCAPI_LOGGER_WARN("extra type " + MarketDataMessage::dataTypeToString(type));
        }
      }
    }
  }
  virtual void alignSnapshot(std::map<Decimal, std::string>& snapshotBid, std::map<Decimal, std::string>& snapshotAsk, int marketDepthSubscribedToExchange) {
    CCAPI_LOGGER_TRACE("snapshotBid.size() = " + toString(snapshotBid.size()));
    if (snapshotBid.size() > marketDepthSubscribedToExchange) {
      keepLastN(snapshotBid, marketDepthSubscribedToExchange);
    }
    CCAPI_LOGGER_TRACE("snapshotBid.size() = " + toString(snapshotBid.size()));
    CCAPI_LOGGER_TRACE("snapshotAsk.size() = " + toString(snapshotAsk.size()));
    if (snapshotAsk.size() > marketDepthSubscribedToExchange) {
      keepFirstN(snapshotAsk, marketDepthSubscribedToExchange);
    }
    CCAPI_LOGGER_TRACE("snapshotAsk.size() = " + toString(snapshotAsk.size()));
  }
  virtual bool checkOrderBookChecksum(const std::map<Decimal, std::string>& snapshotBid, const std::map<Decimal, std::string>& snapshotAsk,
                                      const std::string& receivedOrderBookChecksumStr, bool& shouldProcessRemainingMessage) {
    if (this->sessionOptions.enableCheckOrderBookChecksum) {
      std::string calculatedOrderBookChecksumStr = this->calculateOrderBookChecksum(snapshotBid, snapshotAsk);
      if (!calculatedOrderBookChecksumStr.empty() && calculatedOrderBookChecksumStr != receivedOrderBookChecksumStr) {
        shouldProcessRemainingMessage = false;
        CCAPI_LOGGER_ERROR("calculatedOrderBookChecksumStr = " + calculatedOrderBookChecksumStr);
        CCAPI_LOGGER_ERROR("receivedOrderBookChecksumStr = " + receivedOrderBookChecksumStr);
        CCAPI_LOGGER_ERROR("snapshotBid = " + toString(snapshotBid));
        CCAPI_LOGGER_ERROR("snapshotAsk = " + toString(snapshotAsk));
        return false;
      } else {
        CCAPI_LOGGER_DEBUG("calculatedOrderBookChecksumStr = " + calculatedOrderBookChecksumStr);
        CCAPI_LOGGER_DEBUG("receivedOrderBookChecksumStr = " + receivedOrderBookChecksumStr);
      }
    }
    return true;
  }
  virtual bool checkOrderBookCrossed(const std::map<Decimal, std::string>& snapshotBid, const std::map<Decimal, std::string>& snapshotAsk,
                                     bool& shouldProcessRemainingMessage) {
    if (this->sessionOptions.enableCheckOrderBookCrossed) {
      auto i1 = snapshotBid.rbegin();
      auto i2 = snapshotAsk.begin();
      if (i1 != snapshotBid.rend() && i2 != snapshotAsk.end()) {
        auto& bid = i1->first;
        auto& ask = i2->first;
        if (bid >= ask) {
          CCAPI_LOGGER_ERROR("bid = " + toString(bid));
          CCAPI_LOGGER_ERROR("ask = " + toString(ask));
          shouldProcessRemainingMessage = false;
          return false;
        }
      }
    }
    return true;
  }
  virtual void onIncorrectStatesFound(WsConnection& wsConnection, wspp::connection_hdl hdl, const std::string& textMessage, const TimePoint& timeReceived,
                                      const std::string& exchangeSubscriptionId, std::string const& reason) {
    std::string errorMessage = "incorrect states found: connection = " + toString(wsConnection) + ", textMessage = " + textMessage +
                               ", timeReceived = " + UtilTime::getISOTimestamp(timeReceived) + ", exchangeSubscriptionId = " + exchangeSubscriptionId +
                               ", reason = " + reason;
    CCAPI_LOGGER_ERROR(errorMessage);
    ErrorCode ec;
    this->close(wsConnection, hdl, websocketpp::close::status::normal, "incorrect states found: " + reason, ec);
    if (ec) {
      this->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::GENERIC_ERROR, "shutdown");
    }
    this->shouldProcessRemainingMessageOnClosingByConnectionIdMap[wsConnection.id] = false;
    this->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::INCORRECT_STATE_FOUND, errorMessage);
  }
  int calculateMarketDepthSubscribedToExchange(int depthWanted, std::vector<int> availableMarketDepth) {
    int i = ceilSearch(availableMarketDepth, 0, availableMarketDepth.size(), depthWanted);
    if (i < 0) {
      i = availableMarketDepth.size() - 1;
    }
    return availableMarketDepth[i];
  }
  void setConflateTimer(const TimePoint& previousConflateTp, const std::chrono::milliseconds& interval, const std::chrono::milliseconds& gracePeriod,
                        const WsConnection& wsConnection, const std::string& channelId, const std::string& symbolId, const std::string& field,
                        const std::map<std::string, std::string>& optionMap, const std::vector<std::string>& correlationIdList) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    if (wsConnection.status == WsConnection::Status::OPEN) {
      if (this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap.find(wsConnection.id) != this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap.end() &&
          this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id].find(channelId) !=
              this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id].end() &&
          this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId].find(symbolId) !=
              this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId].end()) {
        this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]->cancel();
      }
      long waitMilliseconds =
          std::chrono::duration_cast<std::chrono::milliseconds>(previousConflateTp + interval + gracePeriod - std::chrono::system_clock::now()).count();
      if (waitMilliseconds > 0) {
        this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = this->serviceContextPtr->tlsClientPtr->set_timer(
            waitMilliseconds,
            [wsConnection, channelId, symbolId, field, optionMap, correlationIdList, previousConflateTp, interval, gracePeriod, this](ErrorCode const& ec) {
              if (this->wsConnectionByIdMap.find(wsConnection.id) != this->wsConnectionByIdMap.end()) {
                if (ec) {
                  CCAPI_LOGGER_ERROR("wsConnection = " + toString(wsConnection) + ", conflate timer error: " + ec.message());
                  this->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::GENERIC_ERROR, ec, "timer");
                } else {
                  if (this->wsConnectionByIdMap.at(wsConnection.id).status == WsConnection::Status::OPEN) {
                    auto conflateTp = previousConflateTp + interval;
                    if (conflateTp > this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId)) {
                      Event event;
                      event.setType(Event::Type::SUBSCRIPTION_DATA);
                      std::vector<Element> elementList;
                      if (field == CCAPI_MARKET_DEPTH) {
                        std::map<Decimal, std::string>& snapshotBid = this->snapshotBidByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId];
                        std::map<Decimal, std::string>& snapshotAsk = this->snapshotAskByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId];
                        this->updateElementListWithUpdateMarketDepth(field, optionMap, snapshotBid, std::map<Decimal, std::string>(), snapshotAsk,
                                                                     std::map<Decimal, std::string>(), elementList, true);
                      } else if (field == CCAPI_TRADE) {
                        this->updateElementListWithOhlc(wsConnection, channelId, symbolId, field, elementList);
                      }
                      CCAPI_LOGGER_TRACE("elementList = " + toString(elementList));
                      this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId) = conflateTp;
                      std::vector<Message> messageList;
                      if (!elementList.empty()) {
                        Message message;
                        message.setTimeReceived(conflateTp);
                        message.setType(Message::Type::MARKET_DATA_EVENTS);
                        message.setRecapType(Message::RecapType::NONE);
                        message.setTime(field == CCAPI_MARKET_DEPTH ? conflateTp : previousConflateTp);
                        message.setElementList(elementList);
                        message.setCorrelationIdList(correlationIdList);
                        messageList.push_back(std::move(message));
                      }
                      if (!messageList.empty()) {
                        event.addMessages(messageList);
                        this->eventHandler(event);
                      }
                    }
                    auto now = UtilTime::now();
                    while (conflateTp + interval + gracePeriod <= now) {
                      conflateTp += interval;
                    }
                    this->setConflateTimer(conflateTp, interval, gracePeriod, wsConnection, channelId, symbolId, field, optionMap, correlationIdList);
                  }
                }
              }
            });
      }
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  virtual void subscribeToExchange(const WsConnection& wsConnection) {
    CCAPI_LOGGER_INFO("exchange is " + this->exchangeName);
    std::vector<std::string> sendStringList = this->createSendStringList(wsConnection);
    for (const auto& sendString : sendStringList) {
      CCAPI_LOGGER_INFO("sendString = " + sendString);
      ErrorCode ec;
      this->send(wsConnection.hdl, sendString, wspp::frame::opcode::text, ec);
      if (ec) {
        this->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::SUBSCRIPTION_FAILURE, ec, "subscribe");
      }
    }
  }
  void processSuccessfulTextMessage(const Request& request, const std::string& textMessage, const TimePoint& timeReceived) override {
    CCAPI_LOGGER_FUNCTION_ENTER;
    std::vector<MarketDataMessage> marketDataMessageList = this->convertTextMessageToMarketDataMessage(request, textMessage, timeReceived);
    CCAPI_LOGGER_TRACE("marketDataMessageList = " + toString(marketDataMessageList));
    if (!marketDataMessageList.empty()) {
      Event event;
      event.setType(Event::Type::RESPONSE);
      for (auto& marketDataMessage : marketDataMessageList) {
        if (marketDataMessage.type == MarketDataMessage::Type::MARKET_DATA_EVENTS) {
          const std::vector<std::string>& correlationIdList = {request.getCorrelationId()};
          CCAPI_LOGGER_TRACE("correlationIdList = " + toString(correlationIdList));
          if (marketDataMessage.data.find(MarketDataMessage::DataType::TRADE) != marketDataMessage.data.end()) {
            auto messageType = this->requestOperationToMessageTypeMap.at(request.getOperation());
            this->processTrade(event, marketDataMessage.tp, timeReceived, marketDataMessage.data, correlationIdList, messageType);
          }
        } else {
          CCAPI_LOGGER_WARN("market data event type is unknown!");
        }
      }
      CCAPI_LOGGER_TRACE("event type is " + event.typeToString(event.getType()));
      this->eventHandler(event);
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  void processTrade(Event& event, const TimePoint& tp, const TimePoint& timeReceived, MarketDataMessage::TypeForData& input,
                    const std::vector<std::string>& correlationIdList, Message::Type messageType) {
    std::vector<Message> messageList;
    std::vector<Element> elementList;
    this->updateElementListWithTrade(CCAPI_TRADE, input, elementList);
    CCAPI_LOGGER_TRACE("elementList = " + toString(elementList));
    Message message;
    message.setTimeReceived(timeReceived);
    message.setType(messageType);
    message.setTime(tp);
    message.setElementList(elementList);
    message.setCorrelationIdList(correlationIdList);
    messageList.push_back(std::move(message));
    event.addMessages(messageList);
  }
  void connect(WsConnection& wsConnection) override {
    CCAPI_LOGGER_FUNCTION_ENTER;
    Service::connect(wsConnection);
    this->instrumentGroupByWsConnectionIdMap.insert(std::pair<std::string, std::string>(wsConnection.id, wsConnection.group));
    CCAPI_LOGGER_DEBUG("this->instrumentGroupByWsConnectionIdMap = " + toString(this->instrumentGroupByWsConnectionIdMap));
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  void onOpen(wspp::connection_hdl hdl) override {
    CCAPI_LOGGER_FUNCTION_ENTER;
    Service::onOpen(hdl);
    WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
    auto instrumentGroup = wsConnection.group;
    for (const auto& subscription : wsConnection.subscriptionList) {
      auto instrument = subscription.getInstrument();
      this->subscriptionStatusByInstrumentGroupInstrumentMap[instrumentGroup][instrument] = Subscription::Status::SUBSCRIBING;
      this->prepareSubscription(wsConnection, subscription);
    }
    CCAPI_LOGGER_INFO("about to subscribe to exchange");
    this->subscribeToExchange(wsConnection);
  }
  void onFail_(WsConnection& wsConnection) override {
    WsConnection thisWsConnection = wsConnection;
    Service::onFail_(wsConnection);
    this->instrumentGroupByWsConnectionIdMap.erase(thisWsConnection.id);
  }
  void clearStates(WsConnection& wsConnection) override {
    Service::clearStates(wsConnection);
    this->fieldByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->optionMapByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->subscriptionListByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->correlationIdListByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap.erase(wsConnection.id);
    this->snapshotBidByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->snapshotAskByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->processedInitialSnapshotByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->processedInitialTradeByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->l2UpdateIsReplaceByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    if (this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap.find(wsConnection.id) != this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap.end()) {
      for (const auto& x : this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id)) {
        for (const auto& y : x.second) {
          y.second->cancel();
        }
      }
      this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    }
    this->openByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->highByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->lowByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->closeByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->orderBookChecksumByConnectionIdSymbolIdMap.erase(wsConnection.id);
  }
  void onClose(wspp::connection_hdl hdl) override {
    CCAPI_LOGGER_FUNCTION_ENTER;
    WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
    this->instrumentGroupByWsConnectionIdMap.erase(wsConnection.id);
    Service::onClose(hdl);
  }
  virtual std::vector<MarketDataMessage> convertTextMessageToMarketDataMessage(const Request& request, const std::string& textMessage,
                                                                               const TimePoint& timeReceived) {
    return {};
  }
  virtual std::vector<MarketDataMessage> processTextMessage(WsConnection& wsConnection, wspp::connection_hdl hdl, const std::string& textMessage,
                                                            const TimePoint& timeReceived) {
    return {};
  }
  virtual std::string calculateOrderBookChecksum(const std::map<Decimal, std::string>& snapshotBid, const std::map<Decimal, std::string>& snapshotAsk) {
    return {};
  }
  virtual std::vector<std::string> createSendStringList(const WsConnection& wsConnection) { return {}; }
  std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> fieldByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::map<std::string, std::string>>>> optionMapByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, int>>> marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::vector<Subscription>>>> subscriptionListByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::vector<std::string>>>> correlationIdListByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::map<Decimal, std::string>>>> snapshotBidByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::map<Decimal, std::string>>>> snapshotAskByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::map<Decimal, std::string>>>>
      previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::map<Decimal, std::string>>>>
      previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, bool>>> processedInitialSnapshotByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, bool>>> processedInitialTradeByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, bool>>> l2UpdateIsReplaceByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, TimePoint>>> previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, TimerPtr>>> conflateTimerMapByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::string>> orderBookChecksumByConnectionIdSymbolIdMap;
  bool shouldAlignSnapshot{};
  std::map<std::string, std::map<std::string, Subscription::Status>> subscriptionStatusByInstrumentGroupInstrumentMap;
  std::map<std::string, std::string> instrumentGroupByWsConnectionIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> openByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, Decimal>>> highByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, Decimal>>> lowByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> closeByConnectionIdChannelIdSymbolIdMap;
  std::string getRecentTradesTarget;
};
} /* namespace ccapi */
#endif
#endif  // INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_H_
