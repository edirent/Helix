import yfinance as yf

def calculate_mstr_valuation(total_btc_holdings, shares_outstanding, debt_billions=0.0):
    """
    计算 MSTR 的溢价率和隐含 BTC 价格
    
    参数:
    total_btc_holdings (float): MSTR 持有的比特币总数
    shares_outstanding (float): MSTR 总流通股数 (单位: 股)
    debt_billions (float): 净债务 (单位: 十亿美元，可选，默认忽略)
    """
    
    print("正在获取实时数据...")
    
    # 1. 获取实时市场价格
    try:
        mstr_ticker = yf.Ticker("MSTR")
        btc_ticker = yf.Ticker("BTC-USD")
        
        # 获取最新收盘价或实时价格
        mstr_price = mstr_ticker.history(period="1d")['Close'].iloc[-1]
        btc_price = btc_ticker.history(period="1d")['Close'].iloc[-1]
    except Exception as e:
        print(f"数据获取失败: {e}")
        return

    # 2. 计算基础指标
    mstr_market_cap = mstr_price * shares_outstanding
    btc_holdings_value = total_btc_holdings * btc_price
    
    # 调整企业价值 (Enterprise Value) - 简易版
    # 如果考虑债务，市值需要加上净债务才等于由于资产支撑的价格
    # 但通常交易者只看市值 vs 持币价值
    
    # 3. 计算关键估值数据
    
    # A. 溢价率 (Premium): (市值 / 持币价值) - 1
    premium = (mstr_market_cap / btc_holdings_value) - 1
    
    # B. 隐含 BTC 价格 (Implied Price): 市值 / 持币量
    implied_btc_price = (mstr_market_cap + (debt_billions * 1e9)) / total_btc_holdings
    
    # C. 每股含币量 (BTC per Share)
    btc_per_share = total_btc_holdings / shares_outstanding * 1000 # 换算成 mBTC/股 方便看
    
    # 4. 输出结果
    print("-" * 40)
    print(f"【MSTR 实时估值分析】")
    print("-" * 40)
    print(f"当前 MSTR 股价: ${mstr_price:,.2f}")
    print(f"当前 BTC 价格:  ${btc_price:,.2f}")
    print("-" * 40)
    print(f"MSTR 总市值:    ${mstr_market_cap/1e9:,.2f} B")
    print(f"持仓 BTC 总价值: ${btc_holdings_value/1e9:,.2f} B")
    print("-" * 40)
    print(f"★ 当前溢价率 (Premium): {premium:.2%}")
    print(f"★ 隐含 BTC 价格: ${implied_btc_price:,.0f} (市场定价)")
    print(f"  (即: 只有当BTC涨到这个价格，MSTR当前股价才算'平价')")
    print("-" * 40)
    print(f"每股含币量: {btc_per_share:.4f} mBTC")
    print("-" * 40)

# ==========================================
# 使用示例：请在此处更新最新数据
# ==========================================

# 注意：以下数据随时间变化，请务必查询最新的 MSTR 财报或 Saylor Tracker
# 示例数据（需替换为当前真实数据）：
LATEST_BTC_HOLDINGS = 402100  # 举例：持币量
LATEST_SHARES_OUT = 250000000 # 举例：流通股数 (需查询 outstanding shares)

# 运行程序
if __name__ == "__main__":
    # 提示用户输入（也可直接硬编码上面的变量）
    try:
        print("请参考 Saylor Tracker 或最新财报输入数据：")
        holdings = float(input(f"请输入 MSTR 持币总量 (默认 {LATEST_BTC_HOLDINGS}): ") or LATEST_BTC_HOLDINGS)
        shares = float(input(f"请输入 MSTR 流通股数 (默认 {LATEST_SHARES_OUT}): ") or LATEST_SHARES_OUT)
        
        calculate_mstr_valuation(holdings, shares)
    except ValueError:
        print("输入无效，请输入数字。")
