"""Embed the locally installed Moonlight SVG as a tiny RGB window icon."""
from pathlib import Path
import subprocess

here = Path(__file__).resolve().parent
source = here / 'overlay/usr/share/icons/hicolor/256x256/apps/moonlight.png'
pc = here / 'overlay/usr/share/moonlight-artwork/desktop_windows-48px.svg'
artwork = here / 'overlay/usr/share/moonlight-artwork'
text = '#ifndef DOVEOS_MOONLIGHT_ICON_H\n#define DOVEOS_MOONLIGHT_ICON_H\n'
for name, path, size, background in (
    ('moonlight_icon_rgb', source, 48, None),
    ('moonlight_pc_rgb', pc, 128, '#2e2e2e'),
    ('moonlight_app_rgb', artwork / 'no_app_image.png', 128, '#2e2e2e'),
    ('moonlight_settings_rgb', artwork / 'settings.svg', 32, '#4054af'),
    ('moonlight_add_rgb', artwork / 'ic_add_to_queue_white_48px.svg', 32, '#4054af'),
    ('moonlight_help_rgb', artwork / 'baseline-help_outline-24px.svg', 32, '#4054af'),
    ('moonlight_back_rgb', artwork / 'arrow_left.svg', 32, '#4054af'),
    ('moonlight_lock_rgb', artwork / 'baseline-lock-24px.svg', 64, '#2e2e2e'),
):
    if path.suffix == '.svg':
        rendered = here / 'artifacts' / f'{name}.png'
        subprocess.check_call(['gdk-pixbuf-thumbnailer', '-s', str(size * 4),
                               str(path), str(rendered)])
        path = rendered
    command = ['convert']
    if background:
        command += ['-size', f'{size}x{size}', f'xc:{background}']
    command += [str(path), '-resize', f'{size}x{size}!']
    if background:
        command += ['-compose', 'Over', '-composite']
    command += ['-alpha', 'remove', '-depth', '8', 'rgb:-']
    pixels = subprocess.check_output(command)
    assert len(pixels) == size * size * 3
    text += f'static const unsigned char {name}[{size}*{size}*3]={{\n'
    for pos in range(0, len(pixels), 24):
        text += ','.join(str(value) for value in pixels[pos:pos+24]) + ',\n'
    text += '};\n'
for name, filename in (
    ('moonlight_desktop_art_rgb', '881448767.png'),
    ('moonlight_steam_art_rgb', '1093255277.png'),
):
    pixels = subprocess.check_output([
        'convert', str(artwork / filename), '-resize', '192x256!',
        '-alpha', 'remove', '-depth', '8', 'rgb:-',
    ])
    assert len(pixels) == 192 * 256 * 3
    text += f'static const unsigned char {name}[192*256*3]={{\n'
    for pos in range(0, len(pixels), 24):
        text += ','.join(str(value) for value in pixels[pos:pos+24]) + ',\n'
    text += '};\n'
text += '#endif\n'
(here / 'moonlight_icon.h').write_text(text)
