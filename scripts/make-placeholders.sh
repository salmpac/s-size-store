#!/usr/bin/env bash
# Generates the placeholder tiles the seeded demo catalog points at.
# Real photos land in the same tree once the content pipeline exists.
set -euo pipefail

root="${1:-media/placeholder}"
mkdir -p "$root"

for i in $(seq 1 12); do
  hue=$((250 + i * 4))
  cat > "$root/$i.svg" <<EOF
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 600 800" width="600" height="800">
  <defs>
    <linearGradient id="g$i" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="hsl($hue 18% 11%)"/>
      <stop offset="100%" stop-color="hsl($hue 30% 17%)"/>
    </linearGradient>
  </defs>
  <rect width="600" height="800" fill="url(#g$i)"/>
  <g fill="none" stroke="hsl($hue 40% 30%)" stroke-width="2" opacity=".5">
    <rect x="150" y="180" width="300" height="440" rx="18"/>
    <path d="M150 300 L80 360 M450 300 L520 360"/>
  </g>
  <text x="300" y="700" font-family="Helvetica,Arial,sans-serif" font-size="26"
        fill="hsl($hue 25% 42%)" text-anchor="middle">фото скоро</text>
</svg>
EOF
done

echo "wrote 12 placeholders to $root"
