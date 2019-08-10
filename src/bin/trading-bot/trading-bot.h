#ifndef K_TRADING_BOT_H_
#define K_TRADING_BOT_H_

extern const char _www_html_index,     _www_ico_favicon,     _www_css_base,
                  _www_gzip_bomb,      _www_mp3_audio_0,     _www_css_light,
                  _www_js_client,      _www_mp3_audio_1,     _www_css_dark;

extern const  int _www_html_index_len, _www_ico_favicon_len, _www_css_base_len,
                  _www_gzip_bomb_len,  _www_mp3_audio_0_len, _www_css_light_len,
                  _www_js_client_len,  _www_mp3_audio_1_len, _www_css_dark_len;

class TradingBot: public KryptoNinja {
  public:
     mQuotingParams qp;
            mOrders orders;
           mButtons button;
      mMarketLevels levels;
    mWalletPosition wallet;
            mBroker broker;
  public:
    TradingBot()
      : qp(*this)
      , orders(*this)
      , button(*this)
      , levels(*this, qp, orders)
      , wallet(*this, qp, orders, button, levels)
      , broker(*this, qp, orders, button, levels, wallet)
    {
      display   = terminal;
      padding   = {3, 6, 1, 2};
      databases = true;
      documents = {
        {"",                                  {&_www_gzip_bomb,   _www_gzip_bomb_len  }},
        {"/",                                 {&_www_html_index,  _www_html_index_len }},
        {"/js/client.min.js",                 {&_www_js_client,   _www_js_client_len  }},
        {"/css/bootstrap.min.css",            {&_www_css_base,    _www_css_base_len   }},
        {"/css/bootstrap-theme-dark.min.css", {&_www_css_dark,    _www_css_dark_len   }},
        {"/css/bootstrap-theme.min.css",      {&_www_css_light,   _www_css_light_len  }},
        {"/favicon.ico",                      {&_www_ico_favicon, _www_ico_favicon_len}},
        {"/audio/0.mp3",                      {&_www_mp3_audio_0, _www_mp3_audio_0_len}},
        {"/audio/1.mp3",                      {&_www_mp3_audio_1, _www_mp3_audio_1_len}}
      };
      arguments = { {
        {"wallet-limit", "AMOUNT", "0",                    "set AMOUNT in base currency to limit the balance,"
                                                           "\n" "otherwise the full available balance can be used"},
        {"lifetime",     "NUMBER", "0",                    "set NUMBER of minimum milliseconds to keep orders open,"
                                                           "\n" "otherwise open orders can be replaced anytime required"},
        {"matryoshka",   "URL",    "https://example.com/", "set Matryoshka link URL of the next UI"},
        {"ignore-sun",   "2",      nullptr,                "do not switch UI to light theme on daylight"},
        {"ignore-moon",  "1",      nullptr,                "do not switch UI to dark theme on moonlight"},
        {"debug-orders", "1",      nullptr,                "print detailed output about exchange messages"},
        {"debug-quotes", "1",      nullptr,                "print detailed output about quoting engine"},
        {"debug-wallet", "1",      nullptr,                "print detailed output about target base position"}
      }, [&](unordered_map<string, variant<string, int, double>> &args) {
        if (arg<int>("debug"))
          args["debug-orders"] =
          args["debug-quotes"] =
          args["debug-wallet"] = 1;
        if (arg<int>("ignore-moon") and arg<int>("ignore-sun"))
          args["ignore-moon"] = 0;
        if (arg<int>("debug-orders") or arg<int>("debug-quotes"))
          args["naked"] = 1;
      } };
    };
  protected:
    void run() override {
      gateway->async.connectivity.write = [&](const Connectivity &rawdata) {
        broker.semaphore.read_from_gw(rawdata);
      };
      gateway->async.wallets.write = [&](const Wallets &rawdata) {
        wallet.read_from_gw(rawdata);
      };
      gateway->async.levels.write = [&](const Levels &rawdata) {
        levels.read_from_gw(rawdata);
        wallet.calcFunds();
        calcQuotes();
      };
      gateway->async.orders.write = [&](const Order &rawdata) {
        orders.read_from_gw(rawdata);
        wallet.calcFundsAfterOrder(orders.updated, &gateway->askForFees);
      };
      gateway->async.trades.write = [&](const Trade &rawdata) {
        levels.stats.takerTrades.read_from_gw(rawdata);
      };
      timer_1s([&](const unsigned int &tick) {
        if (gateway->connected() and !levels.warn_empty()) {
          levels.timer_1s();
          if (!(tick % 60)) {
            levels.timer_60s();
            broker.memory.timer_60s();
          }
          wallet.safety.timer_1s();
          calcQuotes();
        }
      });
    };
  private:
    void calcQuotes() {
      if (broker.ready() and levels.ready() and wallet.ready())
        broker.calcQuotes();
      broker.purge();
    };
    static void terminal();
} K;

void TradingBot::terminal() {
  const vector<Order> openOrders = K.orders.working(true);
  const int x = getmaxx(stdscr),
            y = getmaxy(stdscr),
            yMaxLog = y - K.padding_bottom(1 + max(
                            (int)openOrders.size(),
                            K.broker.semaphore.paused() ? 0 : 2
                          ));
  mvwvline(stdscr, 1, 1, ' ', y-1);
  mvwvline(stdscr, yMaxLog-1, 1, ' ', y-1);
  mvwhline(stdscr, yMaxLog,   1, ' ', x-1);
  int yOrders = yMaxLog;
  for (const auto &it : openOrders) {
    mvwhline(stdscr, ++yOrders, 1, ' ', x-1);
    wattron(stdscr, COLOR_PAIR(it.side == Side::Bid ? COLOR_CYAN : COLOR_MAGENTA));
    mvwaddstr(stdscr, yOrders, 1, (((it.side == Side::Bid ? "BID" : "ASK") + (" > "
      + K.gateway->decimal.amount.str(it.quantity))) + ' ' + K.gateway->base + " at price "
      + K.gateway->decimal.price.str(it.price) + ' ' + K.gateway->quote + " (value "
      + K.gateway->decimal.price.str(abs(it.price * it.quantity)) + ' ' + K.gateway->quote + ")"
    ).data());
    wattroff(stdscr, COLOR_PAIR(it.side == Side::Bid ? COLOR_CYAN : COLOR_MAGENTA));
  }
  while (++yOrders < y) mvwhline(stdscr, yOrders, 1, ' ', x-1);
  mvwaddch(stdscr, 0, 0, ACS_ULCORNER);
  mvwhline(stdscr, 0, 1, ACS_HLINE, max(80, x));
  mvwhline(stdscr, 1, 14, ' ', max(80, x)-14);
  mvwvline(stdscr, 1, 0, ACS_VLINE, yMaxLog);
  mvwvline(stdscr, yMaxLog, 0, ACS_LTEE, y);
  mvwaddch(stdscr, y, 0, ACS_BTEE);
  mvwaddch(stdscr, 0, 12, ACS_RTEE);
  wattron(stdscr, COLOR_PAIR(COLOR_GREEN));
  const string title1 = "   " + K.arg<string>("exchange");
  const string title2 = " " + (K.arg<int>("headless") ? "headless" : "UI at " + K.location()) + ' ';
  wattron(stdscr, A_BOLD);
  mvwaddstr(stdscr, 0, 13, title1.data());
  wattroff(stdscr, A_BOLD);
  mvwaddstr(stdscr, 0, 13+title1.length(), title2.data());
  mvwaddch(stdscr, 0, 14, 'K' | A_BOLD);
  wattroff(stdscr, COLOR_PAIR(COLOR_GREEN));
  mvwaddch(stdscr, 0, 13+title1.length()+title2.length(), ACS_LTEE);
  mvwaddch(stdscr, 0, x-26, ACS_RTEE);
  mvwaddstr(stdscr, 0, x-25, (string(" [   ]: ") + (K.broker.semaphore.paused() ? "Start" : "Stop?") + ", [ ]: Quit!").data());
  mvwaddch(stdscr, 0, x-9, 'q' | A_BOLD);
  wattron(stdscr, A_BOLD);
  mvwaddstr(stdscr, 0, x-23, "ESC");
  wattroff(stdscr, A_BOLD);
  mvwaddch(stdscr, 0, 7, ACS_TTEE);
  mvwaddch(stdscr, 1, 7, ACS_LLCORNER);
  mvwhline(stdscr, 1, 8, ACS_HLINE, 4);
  mvwaddch(stdscr, 1, 12, ACS_RTEE);
  wattron(stdscr, COLOR_PAIR(COLOR_MAGENTA));
  const string baseValue  = K.gateway->decimal.funds.str(K.wallet.base.value),
               quoteValue = K.gateway->decimal.price.str(K.wallet.quote.value);
  wattron(stdscr, A_BOLD);
  waddstr(stdscr, (" " + baseValue + ' ').data());
  wattroff(stdscr, A_BOLD);
  waddstr(stdscr, K.gateway->base.data());
  wattroff(stdscr, COLOR_PAIR(COLOR_MAGENTA));
  wattron(stdscr, COLOR_PAIR(COLOR_GREEN));
  waddstr(stdscr, " or ");
  wattroff(stdscr, COLOR_PAIR(COLOR_GREEN));
  wattron(stdscr, COLOR_PAIR(COLOR_CYAN));
  wattron(stdscr, A_BOLD);
  waddstr(stdscr, quoteValue.data());
  wattroff(stdscr, A_BOLD);
  waddstr(stdscr, (" " + K.gateway->quote + ' ').data());
  wattroff(stdscr, COLOR_PAIR(COLOR_CYAN));
  size_t xLenValue = 14+baseValue.length()+quoteValue.length()+K.gateway->base.length()+K.gateway->quote.length()+7,
         xMaxValue = max(xLenValue+1, 18+title1.length()+title2.length());
  mvwaddch(stdscr, 0, xMaxValue, ACS_TTEE);
  mvwaddch(stdscr, 1, xMaxValue, ACS_LRCORNER);
  mvwhline(stdscr, 1, xLenValue, ACS_HLINE, xMaxValue - xLenValue);
  mvwaddch(stdscr, 1, xLenValue, ACS_LTEE);
  const int yPos = max(1, (y / 2) - 6),
            baseAmount  = round(K.wallet.base.amount  * 10 / K.wallet.base.value),
            baseHeld    = round(K.wallet.base.held    * 10 / K.wallet.base.value),
            quoteAmount = round(K.wallet.quote.amount * 10 / K.wallet.quote.value),
            quoteHeld   = round(K.wallet.quote.held   * 10 / K.wallet.quote.value);
  mvwvline(stdscr, yPos+1, x-3, ' ', 10);
  mvwvline(stdscr, yPos+1, x-4, ' ', 10);
  wattron(stdscr, COLOR_PAIR(COLOR_CYAN));
  mvwvline(stdscr, yPos+11-quoteAmount-quoteHeld, x-4, ACS_VLINE, quoteHeld);
  wattron(stdscr, A_BOLD);
  mvwvline(stdscr, yPos+11-quoteAmount, x-4, ' ' | A_REVERSE, quoteAmount);
  wattroff(stdscr, A_BOLD);
  wattroff(stdscr, COLOR_PAIR(COLOR_CYAN));
  wattron(stdscr, COLOR_PAIR(COLOR_MAGENTA));
  mvwvline(stdscr, yPos+11-baseAmount-baseHeld, x-3, ACS_VLINE, baseHeld);
  wattron(stdscr, A_BOLD);
  mvwvline(stdscr, yPos+11-baseAmount, x-3, ' ' | A_REVERSE, baseAmount);
  wattroff(stdscr, A_BOLD);
  wattroff(stdscr, COLOR_PAIR(COLOR_MAGENTA));
  mvwaddch(stdscr, yPos, x-2, ACS_URCORNER);
  mvwaddch(stdscr, yPos+11, x-2, ACS_LRCORNER);
  mvwaddch(stdscr, yPos, x-5, ACS_ULCORNER);
  mvwaddch(stdscr, yPos+11, x-5, ACS_LLCORNER);
  mvwhline(stdscr, yPos, x-4, ACS_HLINE, 2);
  mvwhline(stdscr, yPos+11, x-4, ACS_HLINE, 2);
  mvwaddch(stdscr, yMaxLog, 0, ACS_LTEE);
  mvwhline(stdscr, yMaxLog, 1, ACS_HLINE, 3);
  mvwaddch(stdscr, yMaxLog, 4, ACS_RTEE);
  mvwaddstr(stdscr, yMaxLog, 5, "< (");
  if (K.broker.semaphore.offline()) {
    wattron(stdscr, COLOR_PAIR(COLOR_RED));
    wattron(stdscr, A_BOLD);
    waddstr(stdscr, "DISCONNECTED");
    wattroff(stdscr, A_BOLD);
    wattroff(stdscr, COLOR_PAIR(COLOR_RED));
    waddch(stdscr, ')');
  } else {
    if (K.broker.semaphore.paused()) {
      wattron(stdscr, COLOR_PAIR(COLOR_YELLOW));
      wattron(stdscr, A_BLINK);
      waddstr(stdscr, "press START to trade");
      wattroff(stdscr, A_BLINK);
      wattroff(stdscr, COLOR_PAIR(COLOR_YELLOW));
      waddch(stdscr, ')');
    } else {
      wattron(stdscr, COLOR_PAIR(COLOR_YELLOW));
      waddstr(stdscr, to_string(openOrders.size()).data());
      wattroff(stdscr, COLOR_PAIR(COLOR_YELLOW));
      waddstr(stdscr, ") Open Orders");
    }
    waddstr(stdscr, " while");
    wattron(stdscr, COLOR_PAIR(COLOR_GREEN));
    waddstr(stdscr, (" 1 " + K.gateway->base + " = ").data());
    wattron(stdscr, A_BOLD);
    waddstr(stdscr, K.gateway->decimal.price.str(K.levels.fairValue).data());
    wattroff(stdscr, A_BOLD);
    waddstr(stdscr, (" " + K.gateway->quote).data());
    wattroff(stdscr, COLOR_PAIR(COLOR_GREEN));
    waddch(stdscr, K.broker.semaphore.paused() ? ' ' : ':');
  }
  mvwaddch(stdscr, y-1, 0, ACS_LLCORNER);
  mvwaddstr(stdscr, 1, 2, string("|/-\\").substr(K.broker.memory.orders_60s % 4, 1).data());
};

#endif
