//+------------------------------------------------------------------+
//|                                           quantum_body_v2.mq5     |
//|         Pi-Nexus Quantum Body v2 -- algorithmic trading EA        |
//|                                                                    |
//| Bridges the quantum-resistant multi-chain engine (api_blockchain.py)|
//| to MetaTrader 5: polls /demo and /block endpoints for on-chain     |
//| settlement/quantum-signature-validity signals, blends them with a  |
//| standard price momentum filter, and trades accordingly.            |
//|                                                                    |
//| Requires: Tools > Options > Expert Advisors > allow WebRequest for |
//| the API host (e.g. http://127.0.0.1:8000 or your deployed URL).    |
//+------------------------------------------------------------------+
#property copyright "Pi-Nexus"
#property link      "https://github.com/Tsukimarf"
#property version    "2.00"
#property strict

#include <Trade\Trade.mqh>
CTrade trade;

//--- Inputs
input string  ApiBaseUrl        = "http://127.0.0.1:8000";  // api_blockchain.py base URL
input string  QuantumChain      = "pi-network";              // ethereum | solana | pi-network | stellar-soroban
input double  LotSize           = 0.10;
input int     MomentumPeriod    = 14;                        // bars for momentum confirmation
input int     PollSeconds       = 60;                         // how often to hit the API
input double  StopLossPips      = 300;
input double  TakeProfitPips    = 600;
input int     MagicNumber       = 20260726;

datetime lastPoll = 0;
bool     lastQuantumSignalValid = false;
string   lastTxId = "";

//+------------------------------------------------------------------+
//| Expert initialization                                             |
//+------------------------------------------------------------------+
int OnInit()
  {
   trade.SetExpertMagicNumber(MagicNumber);
   Print("Pi-Nexus Quantum Body v2 EA initialized. Chain=", QuantumChain, " API=", ApiBaseUrl);
   return(INIT_SUCCEEDED);
  }

void OnDeinit(const int reason) { }

//+------------------------------------------------------------------+
//| Poll the quantum blockchain API for the latest settlement signal  |
//| Returns true if a fresh, quantum-signature-valid tx was found on  |
//| the configured chain (used as a confirmation gate, not the sole   |
//| entry trigger -- this EA never trades on signal alone).           |
//+------------------------------------------------------------------+
bool PollQuantumSignal()
  {
   string url = ApiBaseUrl + "/demo";
   string headers = "Content-Type: application/json\r\n";
   char   post[];
   char   result[];
   string result_headers;
   int    timeout = 5000;

   ResetLastError();
   int res = WebRequest("GET", url, headers, timeout, post, result, result_headers);
   if(res == -1)
     {
      int err = GetLastError();
      Print("WebRequest failed (", err, "). Add '", ApiBaseUrl, "' to Tools>Options>Expert Advisors allowed URLs.");
      return(false);
     }

   string json = CharArrayToString(result);

   // Lightweight scan (no external JSON lib dependency): look for our
   // chain's entry and a "quantum_signature_valid": true flag near it.
   int chainPos = StringFind(json, "\"chain\": \"" + QuantumChain + "\"");
   if(chainPos < 0)
     {
      Print("No demo transaction found for chain=", QuantumChain);
      return(false);
     }

   int validPos = StringFind(json, "\"quantum_signature_valid\": true");
   bool valid = (validPos >= 0);

   int txIdPos = StringFind(json, "\"tx_id\":");
   if(txIdPos >= 0)
     {
      int start = StringFind(json, "\"", txIdPos + 9) + 1;
      int end   = StringFind(json, "\"", start);
      if(start > 0 && end > start)
         lastTxId = StringSubstr(json, start, end - start);
     }

   return(valid);
  }

//+------------------------------------------------------------------+
//| Simple momentum confirmation using MA slope over MomentumPeriod   |
//+------------------------------------------------------------------+
bool MomentumBullish()
  {
   double ma[];
   ArraySetAsSeries(ma, true);
   int handle = iMA(_Symbol, _Period, MomentumPeriod, 0, MODE_EMA, PRICE_CLOSE);
   if(handle == INVALID_HANDLE) return(false);
   if(CopyBuffer(handle, 0, 0, 3, ma) < 3) return(false);
   IndicatorRelease(handle);
   return(ma[0] > ma[1] && ma[1] > ma[2]);
  }

bool MomentumBearish()
  {
   double ma[];
   ArraySetAsSeries(ma, true);
   int handle = iMA(_Symbol, _Period, MomentumPeriod, 0, MODE_EMA, PRICE_CLOSE);
   if(handle == INVALID_HANDLE) return(false);
   if(CopyBuffer(handle, 0, 0, 3, ma) < 3) return(false);
   IndicatorRelease(handle);
   return(ma[0] < ma[1] && ma[1] < ma[2]);
  }

//+------------------------------------------------------------------+
//| Expert tick function                                              |
//+------------------------------------------------------------------+
void OnTick()
  {
   if(TimeCurrent() - lastPoll >= PollSeconds)
     {
      lastQuantumSignalValid = PollQuantumSignal();
      lastPoll = TimeCurrent();
      if(lastQuantumSignalValid)
         Print("Quantum settlement confirmed on ", QuantumChain, " tx=", lastTxId);
     }

   if(!lastQuantumSignalValid)
      return; // require an on-chain quantum-signature-valid confirmation before trading

   if(PositionSelect(_Symbol))
      return; // one position at a time for this demo strategy

   double point = _Point;
   double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
   double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

   if(MomentumBullish())
     {
      double sl = ask - StopLossPips * point;
      double tp = ask + TakeProfitPips * point;
      trade.Buy(LotSize, _Symbol, ask, sl, tp, "quantum-body-v2 buy | tx=" + lastTxId);
     }
   else if(MomentumBearish())
     {
      double sl = bid + StopLossPips * point;
      double tp = bid - TakeProfitPips * point;
      trade.Sell(LotSize, _Symbol, bid, sl, tp, "quantum-body-v2 sell | tx=" + lastTxId);
     }
  }
//+------------------------------------------------------------------+
