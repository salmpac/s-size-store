#!/usr/bin/env bash
# Draws the twelve garment tiles the seeded demo catalog points at.
#
# Tile N matches entry N in the title list in src/admin_cli.cpp — the seeder
# picks both by the same index, so a pair of cargo trousers never ends up
# illustrated by a hoodie. Real photos replace these once the content pipeline
# exists.
set -euo pipefail

root="${1:-media/placeholder}"
mkdir -p "$root"

# Shared chrome: background, frame and caption.
open_svg() {
  cat <<EOF
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 600 800" width="600" height="800">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="0.4" y2="1">
      <stop offset="0%" stop-color="#14141b"/>
      <stop offset="100%" stop-color="#1d1a2b"/>
    </linearGradient>
    <linearGradient id="cloth" x1="0" y1="0" x2="0.6" y2="1">
      <stop offset="0%" stop-color="#343143"/>
      <stop offset="100%" stop-color="#26242f"/>
    </linearGradient>
  </defs>
  <rect width="600" height="800" fill="url(#bg)"/>
  <g fill="url(#cloth)" stroke="#7c4dff" stroke-width="3"
     stroke-linejoin="round" stroke-linecap="round" opacity=".92">
EOF
}

close_svg() {
  cat <<EOF
  </g>
  <text x="300" y="742" font-family="Helvetica,Arial,sans-serif" font-size="23"
        fill="#5b5470" text-anchor="middle" letter-spacing="1">$1</text>
</svg>
EOF
}

draw() {                     # draw <index> <caption> <body-svg>
  { open_svg; printf '%s\n' "$3"; close_svg "$2"; } > "$root/$1.svg"
}

# 1 — hoodie
draw 1 "оверсайз худи" '
    <path d="M205 250 L395 250 L415 650 L185 650 Z"/>
    <path d="M205 250 L135 292 L110 468 L172 488 L190 300 Z"/>
    <path d="M395 250 L465 292 L490 468 L428 488 L410 300 Z"/>
    <path d="M228 252 Q300 172 372 252 Q300 300 228 252 Z"/>
    <path d="M232 480 L368 480 L368 556 L232 556 Z" fill="none"/>
    <path d="M272 262 L266 352 M328 262 L334 352" fill="none" stroke-width="5"/>'

# 2 — cargo trousers
draw 2 "карго-брюки" '
    <path d="M196 200 L404 200 L404 260 L196 260 Z"/>
    <path d="M196 260 L292 260 L286 690 L200 690 Z"/>
    <path d="M308 260 L404 260 L400 690 L314 690 Z"/>
    <path d="M200 372 L272 372 L272 452 L200 452 Z" fill="none"/>
    <path d="M328 372 L400 372 L400 452 L328 452 Z" fill="none"/>
    <path d="M196 230 L404 230" fill="none" stroke-width="2"/>'

# 3 — tactical jacket
draw 3 "тактическая куртка" '
    <path d="M210 232 L390 232 L408 636 L192 636 Z"/>
    <path d="M210 232 L138 276 L114 470 L178 490 L194 292 Z"/>
    <path d="M390 232 L462 276 L486 470 L422 490 L406 292 Z"/>
    <path d="M300 232 L300 636" fill="none" stroke-width="5"/>
    <path d="M232 330 L284 330 L284 396 L232 396 Z" fill="none"/>
    <path d="M316 330 L368 330 L368 396 L316 396 Z" fill="none"/>
    <path d="M248 232 L300 268 L352 232" fill="none"/>'

# 4 — printed longsleeve
draw 4 "лонгслив с принтом" '
    <path d="M222 244 L378 244 L392 628 L208 628 Z"/>
    <path d="M222 244 L152 284 L126 496 L184 512 L204 300 Z"/>
    <path d="M378 244 L448 284 L474 496 L416 512 L396 300 Z"/>
    <path d="M258 246 Q300 288 342 246" fill="none"/>
    <circle cx="300" cy="418" r="58" fill="none" stroke-width="4"/>
    <path d="M270 418 L330 418 M300 388 L300 448" fill="none" stroke-width="4"/>'

# 5 — wide jeans
draw 5 "широкие джинсы" '
    <path d="M190 204 L410 204 L410 268 L190 268 Z"/>
    <path d="M190 268 L294 268 L282 694 L176 694 Z"/>
    <path d="M306 268 L410 268 L424 694 L318 694 Z"/>
    <path d="M300 268 L300 694" fill="none" stroke-width="2"/>
    <path d="M206 276 L252 276 L246 322 L206 318 Z" fill="none"/>
    <path d="M394 276 L348 276 L354 322 L394 318 Z" fill="none"/>'

# 6 — bomber
draw 6 "бомбер" '
    <path d="M212 252 L388 252 L400 590 L200 590 Z"/>
    <path d="M212 252 L144 292 L122 468 L182 486 L198 306 Z"/>
    <path d="M388 252 L456 292 L478 468 L418 486 L402 306 Z"/>
    <path d="M196 590 L404 590 L404 640 L196 640 Z"/>
    <path d="M224 226 L376 226 L376 258 L224 258 Z"/>
    <path d="M300 258 L300 590" fill="none" stroke-width="5"/>
    <path d="M122 468 L182 486 L176 528 L116 508 Z"/>
    <path d="M478 468 L418 486 L424 528 L484 508 Z"/>'

# 7 — nylon shorts
draw 7 "шорты нейлон" '
    <path d="M196 250 L404 250 L404 306 L196 306 Z"/>
    <path d="M196 306 L292 306 L288 530 L202 530 Z"/>
    <path d="M308 306 L404 306 L398 530 L312 530 Z"/>
    <path d="M300 306 L300 452" fill="none" stroke-width="2"/>
    <path d="M232 272 L272 272" fill="none" stroke-width="5"/>
    <path d="M328 272 L368 272" fill="none" stroke-width="5"/>'

# 8 — fleece
draw 8 "флиска" '
    <path d="M212 246 L388 246 L404 632 L196 632 Z"/>
    <path d="M212 246 L142 288 L118 478 L180 496 L196 300 Z"/>
    <path d="M388 246 L458 288 L482 478 L420 496 L404 300 Z"/>
    <path d="M240 222 L360 222 L360 252 L240 252 Z"/>
    <path d="M300 252 L300 404" fill="none" stroke-width="5"/>
    <g fill="none" stroke-width="2" opacity=".55">
      <path d="M226 470 L374 470 M226 508 L374 508 M226 546 L374 546 M226 584 L374 584"/>
    </g>'

# 9 — utility vest
draw 9 "жилет утилитарный" '
    <path d="M210 240 L390 240 L404 636 L196 636 Z"/>
    <path d="M300 240 L300 636" fill="none" stroke-width="5"/>
    <path d="M244 240 L300 292 L356 240" fill="none"/>
    <path d="M222 336 L282 336 L282 404 L222 404 Z" fill="none"/>
    <path d="M318 336 L378 336 L378 404 L318 404 Z" fill="none"/>
    <path d="M222 444 L282 444 L282 528 L222 528 Z" fill="none"/>
    <path d="M318 444 L378 444 L378 528 L318 528 Z" fill="none"/>'

# 10 — chunky knit sweater
draw 10 "свитер крупной вязки" '
    <path d="M206 254 L394 254 L410 640 L190 640 Z"/>
    <path d="M206 254 L132 298 L106 486 L170 504 L190 308 Z"/>
    <path d="M394 254 L468 298 L494 486 L430 504 L410 308 Z"/>
    <path d="M252 256 Q300 300 348 256" fill="none"/>
    <g fill="none" stroke-width="3" opacity=".6">
      <path d="M240 340 q30 26 0 52 q-30 26 0 52 q30 26 0 52 q-30 26 0 52"/>
      <path d="M300 340 q30 26 0 52 q-30 26 0 52 q30 26 0 52 q-30 26 0 52"/>
      <path d="M360 340 q30 26 0 52 q-30 26 0 52 q30 26 0 52 q-30 26 0 52"/>
    </g>'

# 11 — balaclava
draw 11 "балаклава" '
    <path d="M300 210 q110 0 110 150 q0 90 -30 150 l-160 0 q-30 -60 -30 -150 q0 -150 110 -150 Z"/>
    <path d="M222 386 q78 -44 156 0 q-78 44 -156 0 Z" fill="#14141b"/>
    <path d="M220 510 l160 0 l22 90 l-204 0 Z"/>
    <circle cx="262" cy="386" r="11" fill="#7c4dff" stroke="none"/>
    <circle cx="338" cy="386" r="11" fill="#7c4dff" stroke="none"/>'

# 12 — oversize shirt
draw 12 "рубашка оверсайз" '
    <path d="M208 248 L392 248 L406 640 L194 640 Z"/>
    <path d="M208 248 L136 290 L112 476 L174 494 L192 302 Z"/>
    <path d="M392 248 L464 290 L488 476 L426 494 L408 302 Z"/>
    <path d="M252 236 L300 302 L348 236 L322 220 L300 244 L278 220 Z"/>
    <path d="M300 302 L300 640" fill="none" stroke-width="2"/>
    <path d="M232 388 L288 388 L288 452 L232 452 Z" fill="none"/>
    <g fill="#7c4dff" stroke="none">
      <circle cx="300" cy="366" r="6"/><circle cx="300" cy="444" r="6"/>
      <circle cx="300" cy="522" r="6"/><circle cx="300" cy="600" r="6"/>
    </g>'

echo "wrote 12 garment tiles to $root"
