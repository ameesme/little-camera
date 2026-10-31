// Pixel art for the emails, lifted straight out of firmware/src/icons.h so a message about
// the camera is drawn the way the camera would draw it. Rendered as a table of filled cells
// rather than an image: every client blocks remote images and most block data: URIs, and a
// table of black rectangles is the one picture that always arrives.

const CAMERA = [
  '........................',
  '........................',
  '........................',
  '.........######.........',
  '........########........',
  '.......###....###.......',
  '..#######......#######..',
  '.#######........#######.',
  '.##..................##.',
  '.##.......####.......##.',
  '.##......######......##.',
  '.##.....###..###.....##.',
  '.##.....##....##.....##.',
  '.##.....##....##.....##.',
  '.##.....###..###.....##.',
  '.##......######......##.',
  '.##.......####.......##.',
  '.##..................##.',
  '.##..................##.',
  '.######################.',
  '..####################..',
  '........................',
  '........................',
  '........................',
];

/** One `<td>` per run of like pixels, so a 24x24 icon is about a hundred cells rather than 576. */
function row(pixels: string, px: number): string {
  let cells = '';
  for (let x = 0; x < pixels.length; ) {
    let run = 1;
    while (x + run < pixels.length && pixels[x + run] === pixels[x]) run++;
    cells += `<td colspan="${run}" style="height:${px}px;font-size:1px;line-height:${px}px;background:${
      pixels[x] === '#' ? '#000' : '#fff'
    }">&nbsp;</td>`;
    x += run;
  }
  return `<tr style="height:${px}px">${cells}</tr>`;
}

/**
 * The widths come off a `<colgroup>` under `table-layout:fixed`, not off the cells: a `width`
 * attribute on a `td` is only a hint, and a client that ignores it collapses the whole drawing
 * into a few black bars.
 */
export function cameraArt(px = 5): string {
  const side = 24 * px;
  const cols = `<colgroup>${`<col style="width:${px}px">`.repeat(24)}</colgroup>`;
  return `<table role="presentation" cellpadding="0" cellspacing="0" border="0" width="${side}" style="border-collapse:collapse;table-layout:fixed;width:${side}px;margin:0 auto">${cols}${CAMERA.map(
    (r) => row(r, px),
  ).join('')}</table>`;
}
