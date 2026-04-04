/* ============================================================
   Wiser — Liquid Glass Interactive Engine
   ============================================================ */
document.addEventListener('DOMContentLoaded', () => {
    const $ = id => document.getElementById(id);
    const searchInput     = $('search-input');
    const navSearchInput  = $('nav-search-input');
    const searchBtn       = $('search-btn');
    const searchBtnMain   = $('search-btn-main');
    const clearBtn        = $('clear-btn');
    const suggestDropdown = $('suggest-dropdown');
    const navbar          = $('navbar');
    const navLogo         = $('nav-logo');
    const resultsHeader   = $('results-header');
    const resultsEl       = $('results');
    const loader          = $('loader');
    const toastContainer  = $('toast-container');
    const toggleImportBtn = $('toggle-import-btn');
    const importOverlay   = $('import-overlay');
    const importClose     = $('import-close');
    const importBtn       = $('import-btn');
    const uploadBtn       = $('upload-btn');
    const fileInput       = $('file-input');
    const dropZone        = $('drop-zone');
    const fileList        = $('file-list');
    const bokehCanvas     = $('bokeh');
    const starCanvas      = $('starfield');

    /* ─── Canvas Animation Controller ─── */
    let starfieldRAF = 0, bokehRAF = 0;
    function pauseCanvasAnimations() {
        if (starfieldRAF) { cancelAnimationFrame(starfieldRAF); starfieldRAF = 0; }
        if (bokehRAF) { cancelAnimationFrame(bokehRAF); bokehRAF = 0; }
    }

    /* ─── Starfield (dark mode — dynamic parallax + nebula) ─── */
    (function initStarfield() {
        if (!starCanvas) return;
        const ctx = starCanvas.getContext('2d');
        const dpr = window.devicePixelRatio || 1;
        let w, h;
        let layers = [];        // parallax star layers
        let shootingStars = [];
        let nebulae = [];       // soft colored nebula clouds
        let mouseX = 0.5, mouseY = 0.5;  // normalized mouse position
        let time = 0;

        function resize() {
            w = window.innerWidth; h = window.innerHeight;
            starCanvas.width = w * dpr; starCanvas.height = h * dpr;
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        }

        function createLayers() {
            layers = [];
            const configs = [
                { count: 100, rMin: 0.2, rMax: 0.8, speed: 0.003, parallax: 0.01 },
                { count: 40,  rMin: 0.6, rMax: 1.4, speed: 0.008, parallax: 0.025 },
                { count: 12,  rMin: 1.2, rMax: 2.2, speed: 0.015, parallax: 0.05 }
            ];
            const colors = ['#ffffff', '#c8d8ff', '#ffe8c8', '#d0d8ff', '#fff0e0', '#e0c8ff'];
            configs.forEach(cfg => {
                const stars = [];
                for (let i = 0; i < cfg.count; i++) {
                    stars.push({
                        bx: Math.random() * w, by: Math.random() * h,
                        r: Math.random() * (cfg.rMax - cfg.rMin) + cfg.rMin,
                        baseAlpha: Math.random() * 0.6 + 0.4,
                        phase: Math.random() * Math.PI * 2,
                        twinkleSpeed: Math.random() * 0.02 + 0.008,
                        color: colors[Math.floor(Math.random() * colors.length)],
                        // Some stars have cross-flare
                        flare: Math.random() > 0.85
                    });
                }
                layers.push({ stars, parallax: cfg.parallax, drift: cfg.speed });
            });
        }

        function createNebulae() {
            nebulae = [];
            const nColors = [
                'rgba(60,80,180,', 'rgba(120,40,160,', 'rgba(40,100,160,'
            ];
            for (let i = 0; i < 2; i++) {
                nebulae.push({
                    x: Math.random() * w, y: Math.random() * h,
                    r: Math.random() * 250 + 150,
                    color: nColors[Math.floor(Math.random() * nColors.length)],
                    alpha: Math.random() * 0.06 + 0.02,
                    dx: (Math.random() - 0.5) * 0.08,
                    dy: (Math.random() - 0.5) * 0.06,
                    phase: Math.random() * Math.PI * 2
                });
            }
        }

        function spawnShootingStar() {
            if (shootingStars.length >= 3) return;
            const startX = Math.random() * w * 0.7 + w * 0.1;
            const startY = Math.random() * h * 0.3;
            const angle = (Math.random() * 40 + 15) * Math.PI / 180;
            shootingStars.push({
                x: startX, y: startY,
                len: Math.random() * 120 + 80,
                speed: Math.random() * 8 + 5,
                angle,
                life: 1,
                decay: Math.random() * 0.012 + 0.008,
                width: Math.random() * 1.5 + 1
            });
        }

        resize(); createLayers(); createNebulae();
        window.addEventListener('resize', () => { resize(); createLayers(); createNebulae(); });
        document.addEventListener('mousemove', e => {
            mouseX = e.clientX / window.innerWidth;
            mouseY = e.clientY / window.innerHeight;
        });
        setInterval(() => { if (Math.random() < 0.35 && starfieldRAF) spawnShootingStar(); }, 2500);
        starCanvas.addEventListener('resume-starfield', () => {
            if (!starfieldRAF) { starfieldRAF = requestAnimationFrame(draw); }
        });

        let lastFrameTime = 0;
        const FRAME_INTERVAL = 33; // ~30fps

        function draw(timestamp) {
            if (timestamp - lastFrameTime < FRAME_INTERVAL) {
                starfieldRAF = requestAnimationFrame(draw);
                return;
            }
            lastFrameTime = timestamp;
            time += 1;
            ctx.clearRect(0, 0, w, h);

            // Draw nebula clouds
            nebulae.forEach(n => {
                n.x += n.dx; n.y += n.dy;
                n.phase += 0.003;
                if (n.x < -n.r) n.x = w + n.r;
                if (n.x > w + n.r) n.x = -n.r;
                if (n.y < -n.r) n.y = h + n.r;
                if (n.y > h + n.r) n.y = -n.r;
                const pulse = n.alpha * (0.7 + 0.3 * Math.sin(n.phase));
                const grad = ctx.createRadialGradient(n.x, n.y, 0, n.x, n.y, n.r);
                grad.addColorStop(0, n.color + pulse + ')');
                grad.addColorStop(0.5, n.color + (pulse * 0.4) + ')');
                grad.addColorStop(1, n.color + '0)');
                ctx.fillStyle = grad;
                ctx.fillRect(n.x - n.r, n.y - n.r, n.r * 2, n.r * 2);
            });

            // Draw parallax star layers
            const offsetX = (mouseX - 0.5) * 2;
            const offsetY = (mouseY - 0.5) * 2;
            layers.forEach(layer => {
                const px = offsetX * layer.parallax * w;
                const py = offsetY * layer.parallax * h;
                layer.stars.forEach(s => {
                    s.phase += s.twinkleSpeed;
                    s.by -= layer.drift;
                    if (s.by < -10) { s.by = h + 10; s.bx = Math.random() * w; }

                    const x = s.bx + px;
                    const y = s.by + py;
                    const twinkle = 0.4 + 0.6 * Math.sin(s.phase);
                    const alpha = s.baseAlpha * twinkle;

                    ctx.globalAlpha = alpha;
                    ctx.fillStyle = s.color;

                    // Star body (simple circle — no per-star gradient)
                    ctx.beginPath();
                    ctx.arc(x, y, s.r, 0, Math.PI * 2);
                    ctx.fill();
                });
            });

            // Shooting stars
            ctx.globalAlpha = 1;
            shootingStars = shootingStars.filter(ss => {
                ss.x += Math.cos(ss.angle) * ss.speed;
                ss.y += Math.sin(ss.angle) * ss.speed;
                ss.life -= ss.decay;
                if (ss.life <= 0 || ss.x > w + 100 || ss.y > h + 100) return false;

                const tailX = ss.x - Math.cos(ss.angle) * ss.len * ss.life;
                const tailY = ss.y - Math.sin(ss.angle) * ss.len * ss.life;
                const grad = ctx.createLinearGradient(tailX, tailY, ss.x, ss.y);
                grad.addColorStop(0, 'rgba(255,255,255,0)');
                grad.addColorStop(0.4, `rgba(180,200,255,${ss.life * 0.3})`);
                grad.addColorStop(0.8, `rgba(220,235,255,${ss.life * 0.7})`);
                grad.addColorStop(1, `rgba(255,255,255,${ss.life})`);

                ctx.beginPath();
                ctx.moveTo(tailX, tailY);
                ctx.lineTo(ss.x, ss.y);
                ctx.strokeStyle = grad;
                ctx.lineWidth = ss.width;
                ctx.lineCap = 'round';
                ctx.stroke();

                // Bright head glow
                const headGrad = ctx.createRadialGradient(ss.x, ss.y, 0, ss.x, ss.y, 6);
                headGrad.addColorStop(0, `rgba(255,255,255,${ss.life * 0.9})`);
                headGrad.addColorStop(0.3, `rgba(200,220,255,${ss.life * 0.4})`);
                headGrad.addColorStop(1, 'rgba(200,220,255,0)');
                ctx.fillStyle = headGrad;
                ctx.beginPath();
                ctx.arc(ss.x, ss.y, 6, 0, Math.PI * 2);
                ctx.fill();

                return true;
            });

            starfieldRAF = requestAnimationFrame(draw);
        }
        starfieldRAF = requestAnimationFrame(draw);
    })();

    /* ─── Ocean Surface (simplified, behind frost blur) ─── */
    (function initBokeh() {
        if (!bokehCanvas) return;
        const ctx = bokehCanvas.getContext('2d');
        const dpr = Math.min(window.devicePixelRatio || 1, 1);
        let w, h, time = 0;
        let mx = 0.5, tmx = 0.5;

        function resize() {
            w = window.innerWidth; h = window.innerHeight;
            bokehCanvas.width = w * dpr; bokehCanvas.height = h * dpr;
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        }
        resize();
        window.addEventListener('resize', resize);
        bokehCanvas.addEventListener('resume-bokeh', () => {
            if (!bokehRAF) { bokehRAF = requestAnimationFrame(draw); }
        });

        document.addEventListener('mousemove', e => {
            tmx = e.clientX / window.innerWidth;
        });

        const HORIZON = 0.32;

        function waveY(x, t, windBias, amp) {
            return Math.sin(x * 0.004 + t * 0.8 + windBias) * amp
                 + Math.sin(x * 0.009 + t * 1.3) * amp * 0.45
                 + Math.cos(x * 0.006 - t * 0.6) * amp * 0.5;
        }

        function draw() {
            time += 0.014;
            mx += (tmx - mx) * 0.05;
            ctx.clearRect(0, 0, w, h);

            const windBias = (mx - 0.5) * 3;
            const horizonY = h * HORIZON;

            // 4 wave layers, coarse step
            const step = 8;
            for (let layer = 0; layer < 4; layer++) {
                const t = layer / 4;
                const baseY = horizonY + t * (h - horizonY) * 0.9;
                const amp = 10 + t * 30;
                const T = time * (0.5 + layer * 0.18);

                ctx.beginPath();
                ctx.moveTo(-10, h + 10);
                for (let x = -10; x <= w + 10; x += step) {
                    ctx.lineTo(x, baseY + waveY(x + layer * 150, T, windBias, amp));
                }
                ctx.lineTo(w + 10, h + 10);
                ctx.closePath();

                const alpha = 0.12 + t * 0.2;
                ctx.fillStyle = `rgba(${5 + t*30|0}, ${40 + t*100|0}, ${80 + t*80|0}, ${alpha})`;
                ctx.fill();
            }

            // Sun glow (follows mouse)
            const sunX = mx * w, sunY = horizonY - 20;
            const sg = ctx.createRadialGradient(sunX, sunY, 0, sunX, sunY, 180);
            sg.addColorStop(0, 'rgba(255, 248, 220, 0.18)');
            sg.addColorStop(0.4, 'rgba(255, 240, 200, 0.06)');
            sg.addColorStop(1, 'rgba(255, 240, 200, 0)');
            ctx.fillStyle = sg;
            ctx.fillRect(sunX - 180, sunY - 180, 360, 360);

            bokehRAF = requestAnimationFrame(draw);
        }
        bokehRAF = requestAnimationFrame(draw);
    })();

    /* ─── Mouse-Tracking Glass Highlights (throttled) ─── */
    const sceneReflex = $('scene-reflex');
    let rafPending = false;
    const cachedGlasses = document.querySelectorAll('.glass-nav[data-glass], .search-glass[data-glass]');
    document.addEventListener('mousemove', e => {
        if (rafPending) return;
        rafPending = true;
        requestAnimationFrame(() => {
            rafPending = false;
            if (sceneReflex) {
                sceneReflex.style.setProperty('--scene-mx', e.clientX + 'px');
                sceneReflex.style.setProperty('--scene-my', e.clientY + 'px');
            }
            // Update cached glass elements near cursor
            cachedGlasses.forEach(el => {
                const rect = el.getBoundingClientRect();
                const x = e.clientX - rect.left;
                const y = e.clientY - rect.top;
                if (x >= -50 && x <= rect.width + 50 && y >= -50 && y <= rect.height + 50) {
                    el.style.setProperty('--mx', x + 'px');
                    el.style.setProperty('--my', y + 'px');
                }
            });
            // Liquid Glass specular highlight for result cards
            const card = e.target.closest ? e.target.closest('.result-item') : null;
            if (card) {
                const rect = card.getBoundingClientRect();
                card.style.setProperty('--mx', (e.clientX - rect.left) + 'px');
                card.style.setProperty('--my', (e.clientY - rect.top) + 'px');
            }
        });
    });

    /* ─── Subtle 3D Tilt for [data-tilt] ─── */
    document.querySelectorAll('[data-tilt]').forEach(el => {
        el.addEventListener('mouseenter', () => {
            // Pause any GSAP breathing animation so it doesn't fight the tilt
            if (el._breathe) el._breathe.pause();
            el.style.transition = 'transform 0.15s ease-out';
        });
        el.addEventListener('mousemove', e => {
            const rect = el.getBoundingClientRect();
            const dx = (e.clientX - rect.left - rect.width / 2) / (rect.width / 2);
            const dy = (e.clientY - rect.top - rect.height / 2) / (rect.height / 2);
            el.style.transform = `perspective(600px) rotateY(${dx * 3}deg) rotateX(${-dy * 3}deg)`;
        });
        el.addEventListener('mouseleave', () => {
            el.style.transition = 'transform 0.5s cubic-bezier(0.34, 1.56, 0.64, 1)';
            el.style.transform = '';
            // Resume breathing after tilt resets
            if (el._breathe) setTimeout(() => el._breathe.resume(), 500);
        });
    });

    /* ─── GSAP Entrance Animations ─── */
    if (typeof gsap !== 'undefined') {
        // CSS [data-entrance] { opacity:0 } prevents FOUC.
        // Directly-animated elements: GSAP autoAlpha overrides CSS opacity.
        // Container elements (btns, stats): show container, hide children with immediateRender.
        document.querySelectorAll('[data-entrance="btns"], [data-entrance="stats"]').forEach(el => {
            el.style.opacity = '1'; el.style.visibility = 'visible'; el.style.transform = 'none';
        });

        const tl = gsap.timeline({
            defaults: { ease: 'back.out(1.7)', duration: 0.9 }
        });
        tl.fromTo('[data-entrance="title"]',
            { y: 50, scale: 0.85, autoAlpha: 0 },
            { y: 0, scale: 1, autoAlpha: 1, duration: 1.1, ease: 'elastic.out(1, 0.6)' })
          .fromTo('[data-entrance="search"]',
            { y: 35, scale: 0.94, autoAlpha: 0 },
            { y: 0, scale: 1, autoAlpha: 1, ease: 'back.out(2)' }, '-=0.5')
          .fromTo('[data-entrance="btns"] > *',
            { y: 25, autoAlpha: 0 },
            { y: 0, autoAlpha: 1, stagger: 0.12, ease: 'back.out(2.2)', immediateRender: true }, '-=0.35')
          .fromTo('[data-entrance="stats"] > *',
            { y: 25, scale: 0.88, autoAlpha: 0 },
            { y: 0, scale: 1, autoAlpha: 1, stagger: 0.1, ease: 'elastic.out(1, 0.7)', immediateRender: true }, '-=0.3')
          .fromTo('[data-entrance="kbd"]',
            { y: 10, autoAlpha: 0 },
            { y: 0, autoAlpha: 1 }, '-=0.2');

        // Blob floating enhancement with organic motion
        gsap.utils.toArray('.blob').forEach((blob, i) => {
            gsap.to(blob, {
                scale: 1 + Math.random() * 0.18,
                x: (Math.random() - 0.5) * 120,
                y: (Math.random() - 0.5) * 90,
                duration: 18 + i * 4,
                ease: 'sine.inOut',
                yoyo: true,
                repeat: -1,
            });
        });

        // Subtle breathing for stat cards (very gentle ±0.5px)
        gsap.utils.toArray('.stat-card').forEach((card, i) => {
            card._breathe = gsap.to(card, {
                y: -0.5,
                duration: 2.5 + i * 0.4,
                ease: 'sine.inOut',
                yoyo: true,
                repeat: -1,
                delay: i * 0.6,
            });
        });
    } else {
        // GSAP not loaded — remove CSS hiding so elements are visible
        document.querySelectorAll('[data-entrance]').forEach(el => {
            el.style.opacity = '1'; el.style.visibility = 'visible'; el.style.transform = 'none';
        });
    }

    /* ─── Theme System ─── */
    const THEME_KEY = 'wiser_theme';
    const root = document.documentElement;
    const media = window.matchMedia('(prefers-color-scheme: dark)');
    // Collect ALL theme toggle buttons (navbar + hero)
    const allThemeBtns = Array.from(document.querySelectorAll('.theme-switch button[data-mode]'));

    function applyTheme(mode) {
        root.removeAttribute('data-theme');
        if (mode === 'light' || mode === 'dark') root.setAttribute('data-theme', mode);
        allThemeBtns.forEach(b => b.classList.toggle('active', b.dataset.mode === mode));
    }
    function saved() { try { return localStorage.getItem(THEME_KEY) || 'auto'; } catch { return 'auto'; } }
    function save(m)  { try { localStorage.setItem(THEME_KEY, m); } catch {} }
    allThemeBtns.forEach(btn => btn.addEventListener('click', () => {
        save(btn.dataset.mode);
        applyTheme(btn.dataset.mode);
        if (typeof gsap !== 'undefined') gsap.from(btn, { scale: 0.5, rotation: -180, duration: 0.5, ease: 'elastic.out(1, 0.5)' });
    }));
    if (media?.addEventListener) media.addEventListener('change', () => { if (saved() === 'auto') applyTheme('auto'); });
    applyTheme(saved());

    /* ─── Toast ─── */
    function showToast(message, type = 'info', duration = 3500) {
        const t = document.createElement('div');
        t.className = `toast ${type}`;
        const icons = {
            success: '<svg width="16" height="16" viewBox="0 0 24 24" fill="none"><path d="M20 6L9 17l-5-5" stroke="#34c759" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/></svg>',
            error:   '<svg width="16" height="16" viewBox="0 0 24 24" fill="none"><circle cx="12" cy="12" r="9" stroke="#ff3b30" stroke-width="2"/><path d="M15 9l-6 6M9 9l6 6" stroke="#ff3b30" stroke-width="2" stroke-linecap="round"/></svg>',
            info:    '<svg width="16" height="16" viewBox="0 0 24 24" fill="none"><circle cx="12" cy="12" r="9" stroke="currentColor" stroke-width="2"/><path d="M12 16v-4M12 8h.01" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>',
        };
        const iconSpan = document.createElement('span');
        iconSpan.innerHTML = icons[type] || icons.info;
        const msgSpan = document.createElement('span');
        msgSpan.textContent = message;
        t.appendChild(iconSpan);
        t.appendChild(msgSpan);
        toastContainer.appendChild(t);
        requestAnimationFrame(() => t.classList.add('show'));
        setTimeout(() => {
            t.classList.remove('show');
            t.addEventListener('transitionend', () => t.remove());
        }, duration);
    }

    /* ─── Stats ─── */
    function loadStats() {
        fetch('/api/stats').then(r => r.json()).then(data => {
            const docEl = $('stat-docs'), tokenEl = $('stat-tokens'), scoringEl = $('stat-scoring');
            if (docEl)     animNum(docEl, data.document_count || 0);
            if (tokenEl)   animNum(tokenEl, data.total_tokens || 0);
            // Show whichever scoring method the UI toggle currently has selected
            if (scoringEl) {
                const activeBtn = document.querySelector('.scoring-btn.active');
                scoringEl.textContent = activeBtn ? activeBtn.dataset.value.toUpperCase() : (data.scoring_method || 'bm25').toUpperCase();
            }
            const ns = $('nav-stats');
            if (ns) ns.textContent = `${fmt(parseInt(data.document_count)||0)} docs · ${fmt(parseInt(data.total_tokens)||0)} tokens`;
        }).catch(() => {});
    }
    function fmt(n) { return n >= 1e6 ? (n/1e6).toFixed(1)+'M' : n >= 1e3 ? (n/1e3).toFixed(1)+'K' : String(n); }
    function animNum(el, target) {
        if (typeof gsap !== 'undefined') {
            gsap.to({ v: 0 }, { v: target, duration: 1, ease: 'power3.out',
                onUpdate: function() { el.textContent = fmt(Math.round(this.targets()[0].v)); }
            });
        } else {
            const dur = 700, start = performance.now();
            (function tick(now) {
                const t = Math.min((now - start) / dur, 1);
                el.textContent = fmt(Math.round(target * (1 - Math.pow(1 - t, 3))));
                if (t < 1) requestAnimationFrame(tick);
            })(start);
        }
    }
    loadStats();

    /* ─── Clear Button ─── */
    function syncClear() { clearBtn.classList.toggle('hidden', !searchInput.value.trim()); }
    searchInput.addEventListener('input', syncClear);
    syncClear();
    clearBtn.addEventListener('click', () => {
        searchInput.value = ''; searchInput.focus(); syncClear();
        if (typeof gsap !== 'undefined') gsap.from(clearBtn, { scale: 0, rotation: 90, duration: 0.3 });
    });

    /* ─── Autocomplete / Suggest ─── */
    let suggestTimer = null;
    let suggestIdx = -1;
    let suggestItems = [];

    function closeSuggest() {
        suggestDropdown.classList.remove('open');
        suggestDropdown.innerHTML = '';
        suggestIdx = -1;
        suggestItems = [];
    }

    function selectSuggestion(text) {
        searchInput.value = text;
        navSearchInput.value = text;
        syncClear();
        closeSuggest();
        doSearch();
    }

    // Event delegation: single listener for all suggestion clicks
    suggestDropdown.addEventListener('mousedown', e => {
        const item = e.target.closest('.sg-suggest-item');
        if (!item) return;
        e.preventDefault();
        const idx = +item.dataset.idx;
        if (suggestItems[idx]) selectSuggestion(suggestItems[idx].text);
    });

    function renderSuggestions(data) {
        const items = data.suggestions || [];
        if (!items.length) { closeSuggest(); return; }
        suggestItems = items;
        suggestIdx = -1;
        let html = '';
        items.forEach((item, i) => {
            const icon = item.type === 'title'
                ? '<svg class="suggest-icon" viewBox="0 0 24 24" fill="none"><path d="M4 4h16v16H4z" stroke="currentColor" stroke-width="1.5"/><path d="M8 8h8M8 12h6" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/></svg>'
                : '<svg class="suggest-icon" viewBox="0 0 24 24" fill="none"><circle cx="11" cy="11" r="6" stroke="currentColor" stroke-width="1.5"/><path d="M15.5 15.5L19 19" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/></svg>';
            const meta = item.type === 'title' ? '' : `<span class="suggest-meta">${item.docs_count} docs</span>`;
            const escaped = item.text.replace(/</g, '&lt;').replace(/>/g, '&gt;');
            html += `<div class="sg-suggest-item" data-idx="${i}">${icon}<span class="suggest-text">${escaped}</span>${meta}</div>`;
        });
        suggestDropdown.innerHTML = html;
        suggestDropdown.classList.add('open');
    }

    function fetchSuggestions(q) {
        if (!q || q.length < 1) { closeSuggest(); return; }
        fetch(`/api/suggest?q=${encodeURIComponent(q)}&limit=8`)
            .then(r => r.json())
            .then(renderSuggestions)
            .catch(() => closeSuggest());
    }

    searchInput.addEventListener('input', () => {
        clearTimeout(suggestTimer);
        suggestTimer = setTimeout(() => fetchSuggestions(searchInput.value.trim()), 200);
    });

    searchInput.addEventListener('keydown', e => {
        if (!suggestDropdown.classList.contains('open')) return;
        const items = suggestDropdown.querySelectorAll('.sg-suggest-item');
        if (e.key === 'ArrowDown') {
            e.preventDefault();
            suggestIdx = Math.min(suggestIdx + 1, items.length - 1);
            items.forEach((el, i) => el.classList.toggle('active', i === suggestIdx));
        } else if (e.key === 'ArrowUp') {
            e.preventDefault();
            suggestIdx = Math.max(suggestIdx - 1, -1);
            items.forEach((el, i) => el.classList.toggle('active', i === suggestIdx));
        } else if (e.key === 'Enter' && suggestIdx >= 0 && suggestIdx < suggestItems.length) {
            e.preventDefault();
            selectSuggestion(suggestItems[suggestIdx].text);
        } else if (e.key === 'Escape') {
            closeSuggest();
        }
    });

    searchInput.addEventListener('blur', () => { setTimeout(closeSuggest, 150); });

    /* ─── Sync Inputs ─── */
    searchInput.addEventListener('input', () => { navSearchInput.value = searchInput.value; });
    navSearchInput.addEventListener('input', () => { searchInput.value = navSearchInput.value; syncClear(); });
    navSearchInput.addEventListener('keypress', e => { if (e.key === 'Enter') doSearch(); });

    /* ─── Keyboard Shortcuts ─── */
    document.addEventListener('keydown', e => {
        if (e.key === '/' && !['INPUT','TEXTAREA','SELECT'].includes(document.activeElement.tagName)) {
            e.preventDefault();
            const target = document.body.classList.contains('has-results') ? navSearchInput : searchInput;
            target.focus();
            if (typeof gsap !== 'undefined') gsap.from(target.closest('[data-glass]') || target, { scale: 0.98, duration: 0.3, ease: 'back.out(2)' });
        }
        if (e.key === 'Escape') {
            const a = document.activeElement;
            if (a === searchInput || a === navSearchInput) {
                if (a.value) { a.value = ''; syncClear(); } else a.blur();
            }
            if (importOverlay.classList.contains('open')) importOverlay.classList.remove('open');
        }
    });

    /* ─── Home ─── */
    function resumeCanvasAnimations() {
        // Restart loops only if not already running
        if (!starfieldRAF && starCanvas) {
            const ev = new Event('resume-starfield');
            starCanvas.dispatchEvent(ev);
        }
        if (!bokehRAF && bokehCanvas) {
            const ev = new Event('resume-bokeh');
            bokehCanvas.dispatchEvent(ev);
        }
    }
    if (navLogo) navLogo.addEventListener('click', () => {
        document.body.classList.remove('has-results');
        navbar.classList.remove('visible');
        searchInput.value = navSearchInput.value;
        syncClear(); searchInput.focus();
        resumeCanvasAnimations();
        if (typeof gsap !== 'undefined') {
            gsap.fromTo('.hero-center',
                { autoAlpha: 0, y: 30, scale: 0.96 },
                { autoAlpha: 1, y: 0, scale: 1, duration: 0.8, ease: 'elastic.out(1, 0.75)' });
            gsap.fromTo('.search-glass',
                { y: 18, autoAlpha: 0, scale: 0.97 },
                { y: 0, autoAlpha: 1, scale: 1, duration: 0.6, delay: 0.12, ease: 'elastic.out(1, 0.8)' });
        }
    });

    /* ─── Search ─── */
    function doSearch() {
        closeSuggest();
        const query = (searchInput.value || navSearchInput.value).trim();
        if (!query) return;
        if (query.length > 1000) { showToast('搜索关键词过长（最多1000字符）', 'error'); return; }
        pauseCanvasAnimations();
        document.body.classList.add('has-results');
        navbar.classList.add('visible');
        navSearchInput.value = query;
        searchInput.value = query;
        currentPage = 1;
        currentQuery = query;
        executeSearch(query, 1);
    }
    function executeSearch(query, page) {
        resultsEl.innerHTML = '';
        resultsHeader.innerHTML = '';
        showSkeletons();
        const phraseSearch = $('phrase-search').checked;
        const activeScoring = document.querySelector('.scoring-btn.active');
        const scoringMethod = activeScoring ? activeScoring.dataset.value : 'bm25';
        const activeFuzzy = document.querySelector('.fuzzy-btn.active');
        const fuzzyVal = activeFuzzy ? activeFuzzy.dataset.value : '0';
        const params = new URLSearchParams({ q: query, phrase: phraseSearch ? '1' : '0', scoring: scoringMethod, page_size: '20', page: String(page), fuzzy: fuzzyVal });
        fetch(`/api/search?${params}`)
            .then(r => r.json())
            .then(data => displayResults(data, query, page))
            .catch(() => {
                resultsEl.innerHTML = '';
                resultsHeader.innerHTML = '<div class="results-summary"><span class="results-count">搜索失败</span></div>';
                showToast('搜索请求失败', 'error');
            });
    }
    searchBtn.addEventListener('click', doSearch);
    searchBtnMain.addEventListener('click', doSearch);
    searchInput.addEventListener('keypress', e => { if (e.key === 'Enter') doSearch(); });

    /* ─── Scoring Toggle ─── */
    const scoringToggle = $('scoring-toggle');
    if (scoringToggle) scoringToggle.addEventListener('click', e => {
        const btn = e.target.closest('.scoring-btn');
        if (!btn || btn.classList.contains('active')) return;
        scoringToggle.querySelectorAll('.scoring-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        const scoringEl = $('stat-scoring');
        if (scoringEl) scoringEl.textContent = btn.dataset.value.toUpperCase();
        if (typeof gsap !== 'undefined') {
            gsap.fromTo(btn,
                { scale: 0.65 },
                { scale: 1.02, duration: 0.4, ease: 'elastic.out(1, 0.5)' });
            if (scoringEl) gsap.fromTo(scoringEl,
                { scale: 0.8, autoAlpha: 0 },
                { scale: 1, autoAlpha: 1, duration: 0.35, ease: 'back.out(2)' });
        }
    });

    /* ─── Skeletons ─── */
    function showSkeletons() {
        const f = document.createDocumentFragment();
        for (let i = 0; i < 4; i++) {
            const d = document.createElement('div');
            d.className = 'result-item show';
            d.innerHTML = '<div class="skeleton skeleton-title"></div><div class="skeleton skeleton-body"></div><div class="skeleton skeleton-body"></div><div class="skeleton skeleton-body" style="width:65%"></div>';
            f.appendChild(d);
        }
        resultsEl.appendChild(f);
    }

    /* ─── Display Results ─── */
    function displayResults(data, rawQuery, page) {
        resultsEl.innerHTML = '';
        const items = Array.isArray(data) ? data : (Array.isArray(data.results) ? data.results : []);
        const totalHits = data.total_hits ?? items.length;
        const tookMs = data.took_ms;

        const pageSize = 20;
        const totalPages = Math.max(1, Math.ceil(totalHits / pageSize));
        const curPage = page || currentPage || 1;
        let h = '<div class="results-summary">';
        h += `<span class="results-count">找到 <strong>${totalHits}</strong> 条结果</span>`;
        h += `<span class="page-info">第 ${curPage} 页，共 ${totalHits} 条结果</span>`;
        if (tookMs !== undefined) h += `<span class="results-time">${Number(tookMs).toFixed(1)} ms</span>`;
        h += '</div>';
        resultsHeader.innerHTML = h;

        if (!items.length) {
            let noResultHtml = `<div style="text-align:center;padding:60px 20px;color:var(--text-3)">
                <svg width="44" height="44" viewBox="0 0 24 24" fill="none" style="margin-bottom:14px;opacity:0.3"><circle cx="11" cy="11" r="7" stroke="currentColor" stroke-width="1.5"/><path d="M16 16l5 5" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/><path d="M8 11h6" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/></svg>
                <p style="font-size:15px;font-weight:500;margin-bottom:4px">没有找到匹配的文档</p>`;
            if (data.did_you_mean) {
                noResultHtml += `<p style="font-size:13px;margin-top:12px">您是否要搜索：<a href="#" class="did-you-mean-link" data-suggestion="${esc(data.did_you_mean)}" style="color:var(--accent);font-weight:600;text-decoration:underline">${esc(data.did_you_mean)}</a></p>`;
            } else {
                noResultHtml += `<p style="font-size:12px">试试不同的关键词</p>`;
            }
            noResultHtml += `</div>`;
            resultsEl.innerHTML = noResultHtml;
            const dymLink = resultsEl.querySelector('.did-you-mean-link');
            if (dymLink) dymLink.addEventListener('click', e => {
                e.preventDefault();
                searchInput.value = dymLink.dataset.suggestion;
                doSearch();
            });
            return;
        }

        const frag = document.createDocumentFragment();
        items.forEach((item, idx) => {
            const div = document.createElement('div');
            div.className = 'result-item';
            const tokens = Array.isArray(item.matched_tokens) ? item.matched_tokens : [];
            const regex = buildRegex(tokens, rawQuery);

            const title = document.createElement('div');
            title.className = 'result-title';
            title.innerHTML = hl(item.title, regex);

            const body = document.createElement('div');
            body.className = 'result-body';
            const displayText = item.snippet || item.body;
            body.innerHTML = hl(displayText, regex);

            const meta = document.createElement('div');
            meta.className = 'result-meta';
            if (item.id !== undefined) {
                const docId = document.createElement('span');
                docId.className = 'result-doc-id';
                docId.textContent = `doc #${item.id}`;
                meta.appendChild(docId);
            }
            const score = document.createElement('span');
            score.className = 'result-score';
            score.textContent = `Score: ${Number(item.score).toFixed(4)}`;
            meta.appendChild(score);

            if (tokens.length) {
                const mt = document.createElement('span');
                mt.className = 'matched-tokens';
                mt.innerHTML = [...new Set(tokens)].map(t => `<span class="token-tag">${esc(t)}</span>`).join('');
                meta.appendChild(mt);
            }

            div.append(title, body, meta);

            // Click to open document detail with highlighting
            div.addEventListener('click', e => {
                const s = window.getSelection();
                if (s && s.toString().trim()) return;
                if (item.id !== undefined) openDocDetail(item.id, regex);
            });

            frag.appendChild(div);
        });
        resultsEl.appendChild(frag);

        // Single batched GSAP stagger animation instead of N×setTimeout
        const allItems = resultsEl.querySelectorAll('.result-item');
        if (typeof gsap !== 'undefined' && allItems.length) {
            gsap.fromTo(allItems,
                { autoAlpha: 0, y: 24, scale: 0.97 },
                { autoAlpha: 1, y: 0, scale: 1,
                  duration: 0.6, ease: 'elastic.out(1, 0.82)',
                  stagger: { each: 0.04, from: 'start' },
                  clearProps: 'transform',
                  onStart: function() { this.targets().forEach(el => el.classList.add('show')); }
                });
        } else {
            allItems.forEach(el => el.classList.add('show'));
        }

        renderPagination(curPage, totalPages);
    }

    /* ─── Text Helpers ─── */
    function esc(s) { return (s||'').replace(/[&<>'"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;',"'":'&#39;','"':'&quot;'}[c])); }
    function escRe(s) { return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'); }
    function buildRegex(tokens, raw) {
        let aug = (Array.isArray(tokens)?tokens:[]).filter(Boolean);
        const q = (raw||'').trim();
        if (q.length >= 1 && !/\s/.test(q)) { const s = new Set(aug.map(t=>t.toLowerCase())); if (!s.has(q.toLowerCase())) aug.push(q); }
        if (!aug.length) return null;
        aug = [...new Set(aug)].sort((a,b)=>b.length-a.length);
        return new RegExp('(' + aug.map(escRe).join('|') + ')', 'gi');
    }
    function hl(text, regex) { if (!regex) return esc(text); return esc(text).replace(regex, m => `<mark class="hl">${m}</mark>`); }

    /* ─── Import Modal (Tabbed) ─── */
    let selectedFiles = [];
    let activeImportTab = 'file';

    function openImport(tab) {
        importOverlay.classList.add('open');
        if (tab) switchImportTab(tab);
        if (typeof gsap !== 'undefined') gsap.from('.modal-wide', { y: 50, scale: 0.9, duration: 0.55, ease: 'back.out(1.8)' });
    }
    function switchImportTab(tabName) {
        activeImportTab = tabName;
        document.querySelectorAll('.import-tab').forEach(t => t.classList.toggle('active', t.dataset.tab === tabName));
        document.querySelectorAll('.import-tab-content').forEach(c => c.classList.toggle('active', c.id === 'tab-' + tabName));
        // Toggle footer buttons
        uploadBtn.style.display = tabName === 'file' ? '' : 'none';
        addDocSubmit.style.display = tabName === 'manual' ? '' : 'none';
    }
    document.querySelectorAll('.import-tab').forEach(tab => {
        tab.addEventListener('click', () => switchImportTab(tab.dataset.tab));
    });

    toggleImportBtn.addEventListener('click', () => openImport('file'));
    const navImportBtn = $('nav-import-btn');
    if (navImportBtn) navImportBtn.addEventListener('click', () => openImport('file'));
    importClose.addEventListener('click', () => importOverlay.classList.remove('open'));
    importOverlay.addEventListener('click', e => { if (e.target === importOverlay) importOverlay.classList.remove('open'); });

    dropZone.addEventListener('click', () => fileInput.click());
    dropZone.addEventListener('dragover', e => { e.preventDefault(); dropZone.classList.add('drag-over'); });
    dropZone.addEventListener('dragleave', e => { e.preventDefault(); dropZone.classList.remove('drag-over'); });
    dropZone.addEventListener('drop', e => { e.preventDefault(); dropZone.classList.remove('drag-over'); if (e.dataTransfer.files.length) addFiles(e.dataTransfer.files); });
    importBtn.addEventListener('click', e => { e.stopPropagation(); fileInput.click(); });
    fileInput.addEventListener('change', () => { if (fileInput.files.length) addFiles(fileInput.files); fileInput.value = ''; });

    function addFiles(files) {
        const ok = new Set(['.json','.jsonl','.tsv','.xml','.ndjson','.pdf']);
        for (const f of files) {
            const ext = f.name.substring(f.name.lastIndexOf('.')).toLowerCase();
            if (!ok.has(ext)) { showToast(`不支持的格式: ${ext}`, 'error'); continue; }
            if (!selectedFiles.some(sf => sf.name === f.name && sf.size === f.size)) selectedFiles.push(f);
        }
        renderFiles();
    }
    function fmtSize(b) { return b < 1024 ? b+' B' : b < 1048576 ? (b/1024).toFixed(1)+' KB' : (b/1048576).toFixed(1)+' MB'; }
    function renderFiles() {
        fileList.innerHTML = '';
        uploadBtn.disabled = !selectedFiles.length;
        selectedFiles.forEach((f, i) => {
            const d = document.createElement('div');
            d.className = 'file-item';
            const isPdf = f.name.toLowerCase().endsWith('.pdf');
            d.innerHTML = `<span class="file-item-name"><svg width="14" height="14" viewBox="0 0 24 24" fill="none"><path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8l-6-6z" stroke="currentColor" stroke-width="2"/><path d="M14 2v6h6" stroke="currentColor" stroke-width="2"/></svg>${esc(f.name)}${isPdf ? ' <small style="color:var(--accent)">(PDF→文本)</small>' : ''}</span><span class="file-item-size">${fmtSize(f.size)}</span>`;
            const rm = document.createElement('button');
            rm.className = 'file-item-remove';
            rm.innerHTML = '<svg width="12" height="12" viewBox="0 0 24 24" fill="none"><path d="M18 6L6 18M6 6l12 12" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>';
            rm.addEventListener('click', () => { const idx = selectedFiles.indexOf(f); if (idx > -1) selectedFiles.splice(idx, 1); renderFiles(); });
            d.appendChild(rm);
            fileList.appendChild(d);
            if (typeof gsap !== 'undefined') gsap.from(d, { x: -20, opacity: 0, duration: 0.3, ease: 'back.out(1.5)' });
        });
    }

    /* ─── PDF Text Extraction ─── */
    async function extractPdfText(file) {
        if (typeof pdfjsLib === 'undefined') {
            showToast('PDF.js 未加载，无法解析 PDF', 'error');
            return null;
        }
        try {
            pdfjsLib.GlobalWorkerOptions.workerSrc = 'https://cdn.jsdelivr.net/npm/pdfjs-dist@3.11.174/build/pdf.worker.min.js';
            const arrayBuffer = await file.arrayBuffer();
            const pdf = await pdfjsLib.getDocument({ data: arrayBuffer }).promise;
            const pages = [];
            for (let i = 1; i <= pdf.numPages; i++) {
                const page = await pdf.getPage(i);
                const content = await page.getTextContent();
                pages.push(content.items.map(item => item.str).join(' '));
            }
            return { title: file.name.replace(/\.pdf$/i, ''), body: pages.join('\n\n') };
        } catch (e) {
            showToast(`PDF 解析失败: ${file.name}`, 'error');
            return null;
        }
    }

    /* ─── Upload (with PDF pre-processing) ─── */
    uploadBtn.addEventListener('click', async () => {
        if (!selectedFiles.length) return;
        uploadBtn.disabled = true;
        loader.classList.add('active');

        // Separate PDF files from regular files
        const pdfFiles = selectedFiles.filter(f => f.name.toLowerCase().endsWith('.pdf'));
        const regularFiles = selectedFiles.filter(f => !f.name.toLowerCase().endsWith('.pdf'));

        // Process PDFs: extract text and submit as documents
        for (const pf of pdfFiles) {
            const doc = await extractPdfText(pf);
            if (doc && doc.body.trim()) {
                try {
                    await fetch('/api/documents', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ title: doc.title, body: doc.body, author: '' })
                    });
                    showToast(`PDF「${doc.title}」已导入`, 'success');
                } catch { showToast(`PDF「${pf.name}」导入失败`, 'error'); }
            }
        }

        // Process regular files via /api/import
        if (regularFiles.length) {
            const fd = new FormData();
            regularFiles.forEach(f => fd.append('file', f));
            try {
                const data = await fetch('/api/import', { method: 'POST', body: fd }).then(r => r.json());
                const ids = data.task_ids || [];
                if (ids.length) {
                    showToast(`${ids.length} 个文件已提交`, 'info');
                    importOverlay.classList.remove('open');
                    const done = new Set(['success','failed','unsupported']);
                    (function poll(n) {
                        Promise.all(ids.map(id => fetch(`/api/task?id=${encodeURIComponent(id)}`).then(r=>r.json()).catch(()=>({id,status:'failed'}))))
                        .then(res => {
                            if (res.every(r => done.has(r.status)) || n >= 300) {
                                loader.classList.remove('active');
                                const ok = res.filter(r=>r.status==='success').length, fail = res.filter(r=>r.status==='failed').length;
                                let msg = `${ok} 个文件导入成功`;
                                if (fail) msg += `，${fail} 个失败`;
                                showToast(msg, fail ? (ok ? 'info' : 'error') : 'success', 5000);
                                selectedFiles = []; renderFiles(); uploadBtn.disabled = false; loadStats();
                            } else setTimeout(() => poll(n+1), 1000);
                        }).catch(() => setTimeout(() => poll(n+1), 1500));
                    })(0);
                    return; // polling handles loader cleanup
                }
            } catch { showToast('导入失败', 'error'); }
        }

        // Cleanup if only PDFs or no regular files
        loader.classList.remove('active');
        uploadBtn.disabled = false;
        selectedFiles = []; renderFiles(); loadStats();
        if (!regularFiles.length && pdfFiles.length) importOverlay.classList.remove('open');
    });

    /* ─── Manual Add Document (inside import modal) ─── */
    const addDocSubmit = $('add-doc-submit');

    if (addDocSubmit) addDocSubmit.addEventListener('click', () => {
        const title = ($('add-doc-title') || {}).value?.trim();
        const body = ($('add-doc-body') || {}).value?.trim();
        const author = ($('add-doc-author') || {}).value?.trim() || '';
        if (!title || !body) { showToast('请填写标题和内容', 'error'); return; }
        addDocSubmit.disabled = true;
        fetch('/api/documents', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ title, body, author })
        })
        .then(r => r.json())
        .then(data => {
            showToast(`文档「${data.title || title}」已添加 (ID: ${data.id})`, 'success');
            importOverlay.classList.remove('open');
            if ($('add-doc-title')) $('add-doc-title').value = '';
            if ($('add-doc-body')) $('add-doc-body').value = '';
            if ($('add-doc-author')) $('add-doc-author').value = '';
            loadStats();
        })
        .catch(() => showToast('添加文档失败', 'error'))
        .finally(() => { addDocSubmit.disabled = false; });
    });

    /* ─── Button Ripple Effect ─── */
    document.querySelectorAll('.btn-primary, .btn-secondary').forEach(btn => {
        btn.addEventListener('click', function(e) {
            const ripple = document.createElement('span');
            ripple.style.cssText = `position:absolute;border-radius:50%;background:rgba(255,255,255,0.35);transform:scale(0);animation:ripple-out 0.6s ease-out forwards;pointer-events:none;`;
            const rect = this.getBoundingClientRect();
            const size = Math.max(rect.width, rect.height) * 2;
            ripple.style.width = ripple.style.height = size + 'px';
            ripple.style.left = (e.clientX - rect.left - size/2) + 'px';
            ripple.style.top = (e.clientY - rect.top - size/2) + 'px';
            this.style.position = 'relative';
            this.style.overflow = 'hidden';
            this.appendChild(ripple);
            ripple.addEventListener('animationend', () => ripple.remove());
        });
    });

    // Inject ripple keyframes
    const style = document.createElement('style');
    style.textContent = '@keyframes ripple-out{to{transform:scale(1);opacity:0}}';
    document.head.appendChild(style);

    /* ─── Pagination State ─── */
    let currentPage = 1;
    let currentQuery = '';

    function renderPagination(page, totalPages) {
        const bar = $('pagination-bar');
        if (!bar) return;
        if (totalPages <= 1) { bar.innerHTML = ''; return; }

        let html = '';
        html += `<button class="page-btn" data-page="${page - 1}" ${page <= 1 ? 'disabled' : ''}>\u2190 上一页</button>`;

        const maxVisible = 5;
        let startPage = Math.max(1, page - Math.floor(maxVisible / 2));
        let endPage = Math.min(totalPages, startPage + maxVisible - 1);
        if (endPage - startPage + 1 < maxVisible) startPage = Math.max(1, endPage - maxVisible + 1);

        if (startPage > 1) {
            html += `<button class="page-btn" data-page="1">1</button>`;
            if (startPage > 2) html += `<span class="page-info">...</span>`;
        }
        for (let i = startPage; i <= endPage; i++) {
            html += `<button class="page-btn ${i === page ? 'active' : ''}" data-page="${i}">${i}</button>`;
        }
        if (endPage < totalPages) {
            if (endPage < totalPages - 1) html += `<span class="page-info">...</span>`;
            html += `<button class="page-btn" data-page="${totalPages}">${totalPages}</button>`;
        }

        html += `<button class="page-btn" data-page="${page + 1}" ${page >= totalPages ? 'disabled' : ''}>下一页 \u2192</button>`;
        bar.innerHTML = html;

        bar.querySelectorAll('.page-btn:not([disabled])').forEach(btn => {
            btn.addEventListener('click', () => {
                const p = parseInt(btn.dataset.page);
                if (p >= 1 && p <= totalPages && p !== page) {
                    currentPage = p;
                    executeSearch(currentQuery, p);
                    window.scrollTo({ top: 0, behavior: 'smooth' });
                }
            });
        });
    }

    /* ─── Fuzzy Toggle ─── */
    const fuzzyToggle = $('fuzzy-toggle');
    if (fuzzyToggle) fuzzyToggle.addEventListener('click', e => {
        const btn = e.target.closest('.fuzzy-btn');
        if (!btn || btn.classList.contains('active')) return;
        fuzzyToggle.querySelectorAll('.fuzzy-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        if (typeof gsap !== 'undefined') {
            gsap.fromTo(btn, { scale: 0.65 }, { scale: 1.02, duration: 0.4, ease: 'elastic.out(1, 0.5)' });
        }
    });

    /* ─── Document Detail Modal ─── */
    const docDetailOverlay = $('doc-detail-overlay');
    const docDetailBody = $('doc-detail-body');
    const docDetailClose = $('doc-detail-close');
    const docDeleteBtn = $('doc-delete-btn');
    let currentDocId = null;

    function openDocDetail(docId, highlightRegex) {
        if (!docId && docId !== 0) return;
        currentDocId = docId;
        docDetailOverlay.classList.add('open');
        document.body.style.overflow = 'hidden';
        docDetailBody.innerHTML = '<div class="doc-detail-loading">加载中...</div>';
        if (typeof gsap !== 'undefined') gsap.from('#doc-detail-overlay .modal-glass', { y: 50, scale: 0.9, duration: 0.55, ease: 'back.out(1.8)' });

        fetch(`/api/documents/${docId}`)
            .then(r => { if (!r.ok) throw new Error('Not found'); return r.json(); })
            .then(doc => {
                const hlTitle = highlightRegex ? hl(doc.title, highlightRegex) : esc(doc.title);
                const hlBody = highlightRegex ? hl(doc.body, highlightRegex) : esc(doc.body);
                const authorHtml = doc.author ? `<span class="doc-badge">✍ ${esc(doc.author)}</span>` : '';
                docDetailBody.innerHTML = `
                    <div class="doc-detail-title">${hlTitle}</div>
                    <div class="doc-detail-badges">
                        <span class="doc-badge">ID: ${doc.id}</span>
                        <span class="doc-badge">${doc.token_count || 0} tokens</span>
                        ${authorHtml}
                    </div>
                    <div class="doc-detail-body-text">${hlBody}</div>
                `;
            })
            .catch(() => {
                docDetailBody.innerHTML = '<div class="doc-detail-loading">加载文档失败</div>';
            });
    }

    function closeDocDetail() {
        docDetailOverlay.classList.remove('open');
        document.body.style.overflow = '';
        currentDocId = null;
    }

    if (docDetailClose) docDetailClose.addEventListener('click', closeDocDetail);
    if (docDetailOverlay) docDetailOverlay.addEventListener('click', e => { if (e.target === docDetailOverlay) closeDocDetail(); });

    if (docDeleteBtn) docDeleteBtn.addEventListener('click', () => {
        if (currentDocId === null) return;
        if (!confirm('确定删除此文档？')) return;
        fetch(`/api/documents/${currentDocId}`, { method: 'DELETE' })
            .then(r => r.json())
            .then(data => {
                if (data.deleted) {
                    showToast('文档已删除', 'success');
                    closeDocDetail();
                    loadStats();
                    if (currentQuery) executeSearch(currentQuery, currentPage);
                } else {
                    showToast('删除失败', 'error');
                }
            })
            .catch(() => showToast('删除请求失败', 'error'));
    });

    /* ─── Admin Panel ─── */
    const adminOverlay = $('admin-overlay');
    const adminClose = $('admin-close');
    const adminBtn = $('admin-btn');
    const adminFlushBtn = $('admin-flush-btn');
    const adminBackupBtn = $('admin-backup-btn');

    function openAdmin() {
        adminOverlay.classList.add('open');
        loadAdminData();
    }

    function closeAdmin() {
        adminOverlay.classList.remove('open');
    }

    if (adminBtn) adminBtn.addEventListener('click', openAdmin);
    if (adminClose) adminClose.addEventListener('click', closeAdmin);
    if (adminOverlay) adminOverlay.addEventListener('click', e => { if (e.target === adminOverlay) closeAdmin(); });

    function loadAdminData() {
        // Health check
        const healthDot = $('health-dot');
        const healthText = $('health-text');
        if (healthDot) healthDot.className = 'health-dot';
        if (healthText) healthText.textContent = '检查中...';
        fetch('/health')
            .then(r => { if (!r.ok) throw new Error(); return r.json(); })
            .then(() => {
                if (healthDot) healthDot.classList.add('ok');
                if (healthText) healthText.textContent = '运行正常';
            })
            .catch(() => {
                if (healthDot) healthDot.classList.add('err');
                if (healthText) healthText.textContent = '连接异常';
            });

        // System info
        fetch('/api/stats')
            .then(r => r.json())
            .then(data => {
                const grid = $('admin-info-grid');
                if (!grid) return;
                grid.innerHTML = [
                    { label: '文档数量', value: fmt(data.document_count || 0) },
                    { label: '总词元数', value: fmt(data.total_tokens || 0) },
                    { label: '平均文档长度', value: data.avg_document_length !== undefined ? Number(data.avg_document_length).toFixed(1) : '-' },
                    { label: 'N-gram 长度', value: data.token_length !== undefined ? data.token_length : '-' },
                    { label: '压缩方法', value: data.compress_method || data.compression || '-' },
                    { label: '短语搜索', value: data.phrase_search ? '开启' : '关闭' },
                    { label: '评分方法', value: (data.scoring_method || 'bm25').toUpperCase() },
                ].map(c => `<div class="admin-info-card"><div class="info-label">${c.label}</div><div class="info-value">${c.value}</div></div>`).join('');
            })
            .catch(() => {});

        // Task history
        fetch('/api/tasks')
            .then(r => r.json())
            .then(tasks => {
                const container = $('admin-tasks');
                if (!container) return;
                if (!Array.isArray(tasks) || !tasks.length) {
                    container.innerHTML = '<div class="admin-tasks-empty">暂无任务</div>';
                    return;
                }
                container.innerHTML = tasks.map(t => `
                    <div class="task-item">
                        <span class="task-name" title="${esc(t.filename || t.id)}">${esc(t.filename || t.id)}</span>
                        <span class="task-status ${esc(t.status)}">${esc(t.status)}</span>
                    </div>
                `).join('');
            })
            .catch(() => {});
    }

    if (adminFlushBtn) adminFlushBtn.addEventListener('click', () => {
        adminFlushBtn.disabled = true;
        fetch('/api/index/flush', { method: 'POST' })
            .then(r => r.json())
            .then(() => { showToast('索引已刷新', 'success'); loadStats(); })
            .catch(() => showToast('刷新索引失败', 'error'))
            .finally(() => { adminFlushBtn.disabled = false; });
    });

    if (adminBackupBtn) adminBackupBtn.addEventListener('click', () => {
        adminBackupBtn.disabled = true;
        fetch('/api/admin/backup', { method: 'POST' })
            .then(r => r.json())
            .then(data => {
                const path = data.path || data.backup_path || '未知路径';
                showToast(`备份成功: ${path}`, 'success', 5000);
            })
            .catch(() => showToast('备份失败', 'error'))
            .finally(() => { adminBackupBtn.disabled = false; });
    });

    /* ─── Close modals/panels on Escape ─── */
    document.addEventListener('keydown', e => {
        if (e.key === 'Escape') {
            if (docDetailOverlay && docDetailOverlay.classList.contains('open')) closeDocDetail();
            if (importOverlay && importOverlay.classList.contains('open')) importOverlay.classList.remove('open');
            if (adminOverlay && adminOverlay.classList.contains('open')) closeAdmin();
        }
    });
});