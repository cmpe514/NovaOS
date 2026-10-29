// Tailwind 3.4.17, compiled at build time instead of the play CDN the original loaded.
// Content is the *rendered* build output, so tokens are already resolved and every class used in the
// markup or in JS string literals is generated. The safelist repeats the classes the app adds at runtime.
const path = require('node:path');
const out = process.env.NOVA_UI_OUT || path.join(__dirname, 'dist');
// Text-size utilities follow the Settings → Text scale slider (--scale, 0.9–1.2). At 100% they are
// exactly Tailwind's defaults, so the default look is unchanged.
const scaled = (size, lineHeight) => [`calc(${size} * var(--scale, 1))`, { lineHeight: /rem$/.test(lineHeight) ? `calc(${lineHeight} * var(--scale, 1))` : lineHeight }];
module.exports = {
  content: [path.join(out, '**/*.html'), path.join(out, 'js/**/*.js')],
  safelist: ['line-through', 'opacity-50', 'rounded-xl', 'py-3', 'text-sm', 'font-bold', 'font-semibold', 'text-white/60', 'mt-2', 'mt-8', 'mb-3', 'gap-2', 'gap-3', 'shrink-0', 'min-w-0', 'break-words', 'space-y-3'],
  theme: {
    extend: {
      fontSize: {
        xs: scaled('0.75rem', '1rem'), sm: scaled('0.875rem', '1.25rem'), base: scaled('1rem', '1.5rem'),
        lg: scaled('1.125rem', '1.75rem'), xl: scaled('1.25rem', '1.75rem'), '2xl': scaled('1.5rem', '2rem'),
        '3xl': scaled('1.875rem', '2.25rem'), '4xl': scaled('2.25rem', '2.5rem'), '5xl': scaled('3rem', '1'),
        '6xl': scaled('3.75rem', '1'), '7xl': scaled('4.5rem', '1'), '8xl': scaled('6rem', '1'), '9xl': scaled('8rem', '1'),
      },
    },
  },
  plugins: [],
};
