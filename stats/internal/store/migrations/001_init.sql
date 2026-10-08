-- Every event as the game server sent it: the history any future stat can be computed from. The id makes each
-- event count once, however many times it's sent.
CREATE TABLE events (
    id          text PRIMARY KEY,
    type        text        NOT NULL,
    at          timestamptz NOT NULL,
    player      text        NOT NULL DEFAULT '',
    subject     text        NOT NULL DEFAULT '',
    killer_kind text        NOT NULL DEFAULT '',
    count       integer     NOT NULL DEFAULT 0,
    icon        integer     NOT NULL DEFAULT -1
);
CREATE INDEX events_at ON events (at);
CREATE INDEX events_type_at ON events (type, at);

-- Running totals, updated in the same transaction as the events, so the dashboard only reads small tables.
CREATE TABLE counters (
    name  text PRIMARY KEY,  -- players_joined, worlds, chat, boss_defeats
    value bigint NOT NULL DEFAULT 0
);

CREATE TABLE players (
    name                 text PRIMARY KEY,
    first_seen           timestamptz NOT NULL,
    last_seen            timestamptz NOT NULL,
    sessions             integer NOT NULL DEFAULT 0,
    play_seconds         bigint  NOT NULL DEFAULT 0,
    kills                integer NOT NULL DEFAULT 0,  -- mobs
    pvp_kills            integer NOT NULL DEFAULT 0,
    deaths               integer NOT NULL DEFAULT 0,
    longest_life_seconds bigint  NOT NULL DEFAULT 0,
    boss_kills           integer NOT NULL DEFAULT 0,
    deepest_level        integer NOT NULL DEFAULT 0,  -- 0 surface, 1-3 caves, 4 sky
    items_collected      bigint  NOT NULL DEFAULT 0
);

CREATE TABLE item_totals (
    item      text PRIMARY KEY,
    icon      integer NOT NULL DEFAULT -1,
    collected bigint  NOT NULL DEFAULT 0,
    crafted   bigint  NOT NULL DEFAULT 0
);

CREATE TABLE tile_totals (
    tile   text PRIMARY KEY,
    broken bigint NOT NULL DEFAULT 0
);

CREATE TABLE mob_totals (
    mob            text PRIMARY KEY,
    killed         bigint NOT NULL DEFAULT 0,  -- by anyone (or anything)
    players_killed bigint NOT NULL DEFAULT 0
);

CREATE TABLE kills (  -- player versus player
    killer text NOT NULL,
    victim text NOT NULL,
    count  integer NOT NULL DEFAULT 0,
    PRIMARY KEY (killer, victim)
);

CREATE TABLE player_items (
    player    text NOT NULL,
    item      text NOT NULL,
    icon      integer NOT NULL DEFAULT -1,
    collected bigint  NOT NULL DEFAULT 0,
    PRIMARY KEY (player, item)
);

CREATE TABLE player_mobs (
    player text NOT NULL,
    mob    text NOT NULL,
    killed integer NOT NULL DEFAULT 0,
    PRIMARY KEY (player, mob)
);
