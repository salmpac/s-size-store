# s-size: сбор пользовательских метрик

Статус: решено, к реализации.
Дата: 2026-09-13.

## Принцип

Не счётчики, а **поток событий** (append-only). Все цифры — запросы по логу.
Новый вопрос к данным не требует нового кода, только SQL.

Две системы, разные роли:

| | Своя аналитика | Яндекс Метрика |
|---|---|---|
| Источник | сервер + sendBeacon | JS-счётчик |
| Отвечает на | *что* работает | *почему* не работает |
| Точность | 100% на переходах | −10..25% (адблок) |
| Влияет на | ранжирование, выбор вещей, деньги | правки UX |

Решения по деньгам принимаются только по своим данным.

## Воронка

    impression  — карточка была видна (>50%, >1s)
    card_click  — открыл детали
    outbound    — ушёл по ссылке          <- главная метрика
    purchase    — купил (только через CPA-постбэк)

## Идентификация

- `aid`  — httpOnly cookie, UUIDv4, TTL 1 год. Устройство.
- `sid`  — сессия, рвётся после 30 мин неактивности.

Логина в MVP нет. Сырой IP не храним — только `hmac(ip, secret_salt)`.

## Схема

```sql
CREATE TABLE events (
  id          INTEGER PRIMARY KEY,
  ts          INTEGER NOT NULL,      -- unix ms
  anon_id     TEXT    NOT NULL,
  session_id  TEXT    NOT NULL,
  type        TEXT    NOT NULL,      -- impression|card_click|outbound|search
  item_id     INTEGER,
  position    INTEGER,               -- позиция в выдаче, см. ниже
  surface     TEXT,                  -- feed|search|related
  click_token TEXT,                  -- только для outbound, уникален
  source      TEXT,                  -- utm_source / referrer
  ua_class    TEXT,                  -- mobile|desktop|bot
  ip_hash     BLOB,
  payload     TEXT                   -- JSON, всё остальное
);
CREATE INDEX idx_events_ts   ON events(ts);
CREATE INDEX idx_events_item ON events(item_id, type);
CREATE UNIQUE INDEX idx_events_token ON events(click_token) WHERE click_token IS NOT NULL;

CREATE TABLE partners (
  id            INTEGER PRIMARY KEY,
  code          TEXT NOT NULL UNIQUE,
  link_template TEXT NOT NULL,
  enabled       INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE conversions (
  id           INTEGER PRIMARY KEY,
  click_token  TEXT NOT NULL,
  ts           INTEGER NOT NULL,
  order_ref    TEXT,
  amount_kopek INTEGER,
  status       TEXT          -- pending|approved|rejected
);

-- суточная свёртка, дашборд читает только её
CREATE TABLE daily_item_stats (
  day INTEGER, item_id INTEGER,
  impressions INTEGER, card_clicks INTEGER, outbounds INTEGER,
  PRIMARY KEY (day, item_id)
);
```

## position — обязательно

Без позиции нельзя отличить «вещь хорошая» от «вещь была первой на экране».
Position bias испортит любые выводы о том, что заходит.
Фронт обязан передавать позицию в каждом impression и клике.

## Outbound-редирект

Фронт никогда не видит партнёрскую ссылку:

    <a href="/r/8123?p=4&s=feed" rel="noopener" target="_blank">

Сервер на `/r/{item_id}`:
1. генерит click-токен (12 симв. base62) прямо в файбере, без похода в БД;
2. кладёт событие в очередь на батч-запись (не ждёт подтверждения);
3. подставляет шаблон партнёра и отвечает 302.

    Cache-Control: no-store     (иначе повторные клики не видны)
    Referrer-Policy: no-referrer
    302, не 301                 (301 браузер запомнит навсегда)

Шаблоны в `partners.link_template`:

    direct      ->  {url}
    admitad     ->  https://ad.admitad.com/g/{campaign}/?subid={subid}&ulp={url_enc}
    generic_utm ->  {url}?utm_source=ssize&utm_medium=catalog&utm_content={subid}

Смена партнёрки = одна строка в `partners`, каталог не трогаем.

**subid = click-токен, не anon_id.** Непрозрачен, вне нашей БД ничего не значит,
персональных данных партнёру не утекает, влезает в лимиты длины сетей.

## Постбэк

    GET /cpa/postback?subid=<token>&order=...&amount=...&status=...&sign=...

Обязательна подпись/общий секрет — иначе любой нарисует фейковые продажи.
По токену находим outbound-клик -> anon_id, item_id, позиция, время.
Это единственный способ замкнуть воронку до выручки.

**subid кладём с первого дня**, даже без подключённой партнёрки — задним числом
данные не восстановятся.

## Impressions

`IntersectionObserver` (>50% видимости дольше 1с) -> буфер -> раз в 5с
`navigator.sendBeacon('/api/ev', batch)`. Дедуп в рамках сессии, иначе скролл
туда-сюда накрутит показы.

## Запись

Не `INSERT` на событие. Очередь -> DB-поток пишет пачкой раз в секунду в одной
транзакции. Ровно та архитектура, что уже есть в zadachi_backend.

## Боты

Без фильтрации 30-50% статистики — чужие краулеры.
Минимум: отсев по UA, требование наличия cookie, rate-limit по anon_id.

## Хранение

Сырые `events` — 90 дней, дальше только `daily_item_stats`.

## Дашборд

Не строим. Страница `/admin/stats` с захардкоженными запросами:
воронка за 7 дней, топ вещей по CTR, CTR по тегам, D1/D7 retention,
разбивка по источникам.

## Открытые вопросы

- [ ] Северная звезда: предложено `outbound-клики на активного пользователя в неделю`.
- [ ] Cookie-баннер + политика обработки ПД (обязательно из-за Метрики, 152-ФЗ).
- [ ] Номер счётчика Метрики.
