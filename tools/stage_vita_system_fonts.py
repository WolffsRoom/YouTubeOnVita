"""Stage Cobalt's official 'limited' font package as the Vita system fonts.

The Evergreen CRX ships an empty fonts.xml ("empty" package): the platform
must provide fonts through kSbSystemPathFontDirectory and
kSbSystemPathFontConfigurationDirectory. This uses the upstream
starboard/content/fonts/scripts/filter_fonts.py with the exact category
values of the upstream 'limited' profile (variables.gni), so the result is
the same package a Starboard build would produce.

Output: experiments/cobalt/vita/system_fonts/{fonts.xml, *.ttf/otf/woff2,
LICENSES.txt}
"""
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
FONTS = ROOT / 'experiments/cobalt/upstream-25/starboard/content/fonts'
OUT = ROOT / 'experiments/cobalt/vita/system_fonts'
LIMITED = [
    'sans-serif=2', 'serif=0', 'fcc-captions=0', 'fallback-lang-non-cjk=1',
    'fallback-lang-cjk=0', 'fallback-lang-cjk-low-quality=1',
    'fallback-historic=0', 'fallback-color-emoji=0', 'fallback-emoji=1',
    'fallback-symbols=1',
]


def main():
  script = FONTS / 'scripts/filter_fonts.py'
  xml = FONTS / 'config/common/fonts.xml'
  if OUT.exists():
    shutil.rmtree(OUT)
  OUT.mkdir(parents=True)
  subprocess.check_call([sys.executable, '-I', str(script), '-i', str(xml),
                         '-o', str(OUT / 'fonts.xml')] + LIMITED)
  listing = subprocess.check_output(
      [sys.executable, '-I', str(script), '-i', str(xml), '-f',
       str(FONTS / 'font_files')] + LIMITED, text=True)
  total = 0
  files = [pathlib.Path(l.strip()) for l in listing.splitlines() if l.strip()]
  for f in files:
    shutil.copy2(f, OUT / f.name)
    total += f.stat().st_size
  shutil.copy2(FONTS / 'LICENSES.txt', OUT / 'LICENSES.txt')
  print(f'staged {len(files)} font files, {total / 1048576:.1f} MiB -> {OUT}')


if __name__ == '__main__':
  main()
