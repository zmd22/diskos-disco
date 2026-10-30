'use strict';
/* Each theme owns its own preview. No autoplay, tracking or external libraries. */
document.querySelectorAll('.theme').forEach(theme => {
  const main = theme.querySelector('.main-shot');
  const image = main.querySelector('img');
  const caption = theme.querySelector('.screen-caption');
  theme.querySelectorAll('[data-screen]').forEach(button => {
    button.addEventListener('click', () => {
      const previous = {src: image.getAttribute('src'), alt: image.alt, caption: caption.textContent};
      image.src = button.dataset.screen;
      image.alt = button.querySelector('img').alt;
      caption.textContent = button.dataset.caption;
      main.href = button.dataset.screen;
      main.setAttribute('aria-label', `Enlarge ${theme.querySelector('h2').textContent} ${caption.textContent}`);
      button.dataset.screen = previous.src;
      button.dataset.caption = previous.caption;
      button.querySelector('img').src = previous.src;
      button.querySelector('img').alt = previous.alt;
      button.querySelector('span').textContent = previous.caption;
      button.setAttribute('aria-label', `Preview ${theme.querySelector('h2').textContent} ${previous.caption}`);
    });
  });
});
const dialog = document.querySelector('.screen-dialog');
if (dialog && typeof dialog.showModal === 'function') {
  document.querySelectorAll('.main-shot').forEach(link => {
    link.addEventListener('click', event => {
      event.preventDefault();
      dialog.querySelector('img').src = link.href;
      dialog.querySelector('img').alt = link.querySelector('img').alt;
      dialog.querySelector('p').textContent = `${link.closest('.theme').querySelector('h2').textContent} · ${link.closest('.theme').querySelector('.screen-caption').textContent}`;
      dialog.showModal();
    });
  });
  dialog.querySelector('button').addEventListener('click', () => dialog.close());
  dialog.addEventListener('click', event => { if (event.target === dialog) dialog.close(); });
}

(() => {
  const v = document.querySelector('.showcase-video'); if (!v) return;
  const btns = [...document.querySelectorAll('.chapters button')];
  const times = btns.map(b => +b.dataset.t);
  btns.forEach(b => b.addEventListener('click', () => { v.currentTime = +b.dataset.t; v.play().catch(() => {}); }));
  v.addEventListener('timeupdate', () => {
    let k = -1; times.forEach((t, i) => { if (v.currentTime >= t - 0.2 && (k < 0 || t >= times[k])) k = i; });
    btns.forEach((b, i) => b.classList.toggle('active', i === k && !v.paused || i === k && v.currentTime > 0));
  });
})();

/* Actual firmware captures, kept in the selected accent across the whole tour. */
(() => {
  const hero = document.querySelector('.zune-hero'); if (!hero) return;
  const steps = [
    ['menu','Main menu','Start with the music.','Big, flowing type puts Now Playing, your library and shortcuts within easy reach.','Swipe up on the player to return Home.'],
    ['music','Now Playing','Room for the song.','The cover, title and artist share the spotlight. Playback controls, progress and audio details are always close.','Choose your controls and shortcuts in Display → Zune settings.'],
    ['coverflow','Library · Coverflow','Take your covers for a spin.','Flip through a carousel of covers. Stop on an album, then dive into its tracks.','On the player: choose Coverflow in the Album View setting.'],
    ['wall','Library · Wall','A record shop in miniature.','A wall of artwork turns your library into a record shop. See more of your collection at a glance.','On the player: choose Wall in the Album View setting.'],
    ['quick','Quick Settings','The little things, close by.','Your most-used controls, one pull away. Choose the shortcuts that belong here.','On the player: customise your Quick Settings items.'],
    ['stats','Listening stats','See where the music takes you.','Your favourite artists and albums, brought together with artwork. Look back over a week, a month or a year.','Sample history is shown here; your Disc builds its own story.'],
    ['immersive','Immersive 3.0','More music. Less in the way.','A spinning record fills the screen. Tap for transport controls, then let the artwork take over again.','Shown with the transport overlay open. Tap the artwork on your Disc to reveal it.']
  ];
  const palette = {magenta:'#ec008c',cyan:'#00b7d8',orange:'#ff751f',lime:'#9bd238'};
  let step = 0, accent = 'magenta';
  const image = hero.querySelector('#zune-screen'), link = hero.querySelector('.zune-enlarge');
  function render() {
    const [file,name,title,copy,hint] = steps[step];
    const src = `site/zune/${accent}-${file}.webp`;
    image.src = src; image.alt = `Zune ${name}, ${accent} accent, sample music and artwork`;
    link.href = src; link.setAttribute('aria-label',`Enlarge Zune ${name}`);
    hero.style.setProperty('--zune-accent',palette[accent]);
    hero.querySelector('#zune-color-name').textContent = accent[0].toUpperCase()+accent.slice(1);
    hero.querySelector('#zune-counter').textContent = `${String(step+1).padStart(2,'0')} / 07 · ${name}`;
    hero.querySelector('#zune-step-title').textContent = title;
    hero.querySelector('#zune-step-copy').textContent = copy;
    hero.querySelector('#zune-step-hint').textContent = hint;
    hero.querySelectorAll('[data-step]').forEach(b=>b.setAttribute('aria-pressed',String(+b.dataset.step===step)));
    hero.querySelectorAll('[data-accent]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.accent===accent)));
  }
  hero.querySelectorAll('[data-step]').forEach(b=>b.addEventListener('click',()=>{step=+b.dataset.step;render();}));
  hero.querySelectorAll('[data-accent]').forEach(b=>b.addEventListener('click',()=>{accent=b.dataset.accent;render();}));
  function advance(delta){step=(step+delta+steps.length)%steps.length;render();}
  hero.querySelector('#zune-prev').addEventListener('click',()=>advance(-1));
  hero.querySelector('#zune-next').addEventListener('click',()=>advance(1));
  hero.querySelector('.zune-steps').addEventListener('keydown',e=>{
    if(e.key==='ArrowRight'||e.key==='ArrowLeft'){e.preventDefault();advance(e.key==='ArrowRight'?1:-1);hero.querySelector(`[data-step="${step}"]`).focus();}
  });
  link.addEventListener('click',e=>{
    if(!dialog || typeof dialog.showModal!=='function')return;
    e.preventDefault();dialog.querySelector('img').src=link.href;dialog.querySelector('img').alt=image.alt;
    dialog.querySelector('p').textContent=`Zune · ${steps[step][1]} · ${accent}`;dialog.showModal();
  });
})();

/* Collection views use actual captures, without intercepting page scrolling. */
document.querySelectorAll('[data-gallery]').forEach(card => {
  const link = card.querySelector('.gallery-enlarge');
  const image = link.querySelector('img');
  const caption = card.querySelector('.gallery-caption');
  const buttons = [...card.querySelectorAll('[data-image]')];
  buttons.forEach(button => button.addEventListener('click', () => {
    image.src = button.dataset.image;
    image.alt = `${button.dataset.caption}, Disco 3.2.2 with illustrated demo artwork`;
    link.href = button.dataset.image;
    link.setAttribute('aria-label', `Enlarge ${button.dataset.caption}`);
    caption.textContent = button.dataset.caption;
    buttons.forEach(b => b.setAttribute('aria-pressed', String(b === button)));
  }));
  card.querySelector('.gallery-options').addEventListener('keydown', event => {
    if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return;
    const index = buttons.indexOf(document.activeElement);
    if (index < 0) return;
    event.preventDefault();
    const next = buttons[(index + (event.key === 'ArrowRight' ? 1 : -1) + buttons.length) % buttons.length];
    next.click(); next.focus();
  });
  link.addEventListener('click', event => {
    if (!dialog || typeof dialog.showModal !== 'function') return;
    event.preventDefault();
    dialog.querySelector('img').src = link.href;
    dialog.querySelector('img').alt = image.alt;
    dialog.querySelector('p').textContent = caption.textContent;
    dialog.showModal();
  });
});
