\set ON_ERROR_STOP on

--access table through fincore-namespace(as in code design)
CREATE SCHEMA IF NOT EXISTS fincore;
REVOKE ALL ON SCHEMA fincore FROM PUBLIC;

CREATE OR REPLACE FUNCTION fincore.set_updated_at()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
    NEW.updated_at := clock_timestamp();
    RETURN NEW;
END;
$$;

-- Instruments table ---
CREATE TABLE IF NOT EXISTS fincore.instruments (
    symbol                  VARCHAR(16) PRIMARY KEY,
    name                    VARCHAR(100) NOT NULL,
    asset_class             VARCHAR(20) NOT NULL,
    exchange                VARCHAR(32) NOT NULL,
    tick_size_decimals      SMALLINT NOT NULL,
    is_active               BOOLEAN NOT NULL DEFAULT TRUE,
    created_at              TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at              TIMESTAMPTZ NOT NULL DEFAULT now(),

    CONSTRAINT instruments_symbol_format_ck
        CHECK (symbol = upper(symbol)
               AND symbol ~ '^[A-Z0-9._:/-]+$'),
    CONSTRAINT instruments_name_nonempty_ck CHECK (btrim(name) <> ''),
    CONSTRAINT instruments_asset_class_ck
        CHECK (asset_class IN ('EQUITY', 'FOREX', 'CRYPTO', 'FUTURES', 'ETF')),
    CONSTRAINT instruments_tick_decimals_ck
        CHECK (tick_size_decimals BETWEEN 0 AND 8)
);

-- Trigger for instruments table to update by calling set_updated_at() function
DROP TRIGGER IF EXISTS trg_instruments_updated_at ON fincore.instruments;
CREATE TRIGGER trg_instruments_updated_at
BEFORE UPDATE ON fincore.instruments
FOR EACH ROW EXECUTE FUNCTION fincore.set_updated_at();


-- Data_Sources table (Alpha_Vantage etc)
CREATE TABLE IF NOT EXISTS fincore.data_sources (
    code            VARCHAR(20) PRIMARY KEY,
    display_name    VARCHAR(60) NOT NULL,
    base_url        TEXT,
    is_active       BOOLEAN NOT NULL DEFAULT TRUE,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT now(),

    CONSTRAINT data_sources_code_format_ck
        CHECK (code = upper(code)
               AND code ~ '^[A-Z0-9_-]+$'),
    CONSTRAINT data_sources_name_nonempty_ck CHECK (btrim(display_name) <> '')
);

DROP TRIGGER IF EXISTS trg_data_sources_updated_at ON fincore.data_sources;
CREATE TRIGGER trg_data_sources_updated_at
BEFORE UPDATE ON fincore.data_sources
FOR EACH ROW EXECUTE FUNCTION fincore.set_updated_at();

--Quotes Table
CREATE TABLE IF NOT EXISTS fincore.quotes (
    symbol      VARCHAR(16) NOT NULL,
    source      VARCHAR(20) NOT NULL,
    timestamp   TIMESTAMPTZ NOT NULL,
    price       NUMERIC(20,8) NOT NULL,
    open        NUMERIC(20,8) NOT NULL,
    high        NUMERIC(20,8) NOT NULL,
    low         NUMERIC(20,8) NOT NULL,
    volume      NUMERIC(24,8) NOT NULL,
    change_pct  NUMERIC(20,8),

    CONSTRAINT quotes_pk PRIMARY KEY (symbol, source, timestamp),
    CONSTRAINT quotes_symbol_fk FOREIGN KEY (symbol)
        REFERENCES fincore.instruments(symbol) ON DELETE RESTRICT,
    CONSTRAINT quotes_source_fk FOREIGN KEY (source)
        REFERENCES fincore.data_sources(code) ON DELETE RESTRICT,
    CONSTRAINT quotes_prices_nonnegative_ck
        CHECK (price >= 0 AND open >= 0 AND high >= 0 AND low >= 0),
    CONSTRAINT quotes_high_low_ck CHECK (high >= low),
    CONSTRAINT quotes_volume_nonnegative_ck CHECK (volume >= 0)
);


--Ticks table
CREATE TABLE IF NOT EXISTS fincore.ticks (
    symbol      VARCHAR(16) NOT NULL,
    source      VARCHAR(20) NOT NULL,
    timestamp   TIMESTAMPTZ NOT NULL,
    trade_id    VARCHAR(64),
    price       NUMERIC(20,8) NOT NULL,
    size        NUMERIC(24,8) NOT NULL,
    side        CHAR(1) NOT NULL DEFAULT 'U',

    CONSTRAINT ticks_symbol_fk FOREIGN KEY (symbol)
        REFERENCES fincore.instruments(symbol) ON DELETE RESTRICT,
    CONSTRAINT ticks_source_fk FOREIGN KEY (source)
        REFERENCES fincore.data_sources(code) ON DELETE RESTRICT,
    CONSTRAINT ticks_price_positive_ck CHECK (price > 0),
    CONSTRAINT ticks_size_positive_ck CHECK (size > 0),
    CONSTRAINT ticks_side_ck CHECK (side IN ('B', 'A', 'U')),
    CONSTRAINT ticks_trade_id_nonempty_ck
        CHECK (trade_id IS NULL OR btrim(trade_id) <> '')
);

CREATE UNIQUE INDEX IF NOT EXISTS ticks_trade_id_uq
    ON fincore.ticks (source, symbol, trade_id)
    WHERE trade_id IS NOT NULL;

--Order_Book_SnapShots table
CREATE TABLE IF NOT EXISTS fincore.order_book_snapshots (
    symbol         VARCHAR(16) NOT NULL,
    source         VARCHAR(20) NOT NULL,
    snapshot_time  TIMESTAMPTZ NOT NULL,
    best_bid       NUMERIC(20,8) NOT NULL,
    best_ask       NUMERIC(20,8) NOT NULL,
    mid_price      NUMERIC(20,8)
        GENERATED ALWAYS AS ((best_bid + best_ask) / 2) STORED,
    spread         NUMERIC(20,8)
        GENERATED ALWAYS AS (best_ask - best_bid) STORED,
    imbalance      NUMERIC(20,8) NOT NULL,
    total_bid_vol  NUMERIC(24,8) NOT NULL,
    total_ask_vol  NUMERIC(24,8) NOT NULL,

    CONSTRAINT order_book_snapshots_pk
        PRIMARY KEY (symbol, source, snapshot_time),
    CONSTRAINT order_book_snapshots_symbol_fk FOREIGN KEY (symbol)
        REFERENCES fincore.instruments(symbol) ON DELETE RESTRICT,
    CONSTRAINT order_book_snapshots_source_fk FOREIGN KEY (source)
        REFERENCES fincore.data_sources(code) ON DELETE RESTRICT,
    CONSTRAINT order_book_prices_positive_ck CHECK (best_bid > 0 AND best_ask > 0),
    CONSTRAINT order_book_not_crossed_ck CHECK (best_ask >= best_bid),
    CONSTRAINT order_book_imbalance_ck CHECK (imbalance BETWEEN -1 AND 1),
    CONSTRAINT order_book_volumes_nonnegative_ck
        CHECK (total_bid_vol >= 0 AND total_ask_vol >= 0)
);

CREATE TABLE IF NOT EXISTS fincore.technical_indicators (
    symbol          VARCHAR(16) NOT NULL,
    source          VARCHAR(20) NOT NULL,
    indicator_name  VARCHAR(32) NOT NULL,
    timestamp       TIMESTAMPTZ NOT NULL,
    value           NUMERIC(20,8) NOT NULL,
    parameters      JSONB NOT NULL DEFAULT '{}'::jsonb,

    --update on delete all other tables based on technical indicators data
    CONSTRAINT technical_indicators_pk
        PRIMARY KEY (symbol, source, indicator_name, timestamp, parameters),
    CONSTRAINT technical_indicators_symbol_fk FOREIGN KEY (symbol)
        REFERENCES fincore.instruments(symbol) ON DELETE RESTRICT,
    CONSTRAINT technical_indicators_source_fk FOREIGN KEY (source)
        REFERENCES fincore.data_sources(code) ON DELETE RESTRICT,

    CONSTRAINT technical_indicators_name_ck
        CHECK (indicator_name = upper(indicator_name)
               AND indicator_name ~ '^[A-Z0-9_]+$'),
    CONSTRAINT technical_indicators_parameters_object_ck
        CHECK (jsonb_typeof(parameters) = 'object')
);


COMMENT ON TABLE fincore.ticks IS
    'Ticks intentionally have no synthetic primary key. Source trade_id is used for deduplication when available.';
COMMENT ON COLUMN fincore.ticks.side IS
    'Aggressor side: B=buyer initiated, A=seller/ask initiated, U=unknown.';
