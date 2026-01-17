/*
  文件：script.js
  作用：Wiser Web 前端交互逻辑
  - 主题切换：auto/light/dark，写入 localStorage 并同步 meta color-scheme
  - 搜索：调用 /api/search，渲染结果并对命中词元高亮
  - 导入：拖拽/选择文件，调用 /api/import 创建任务并轮询 /api/task 状态
*/

document.addEventListener('DOMContentLoaded', function () {
    // THEME: toggle and persistence
    const THEME_KEY = 'wiser_theme_preference'; // 'auto' | 'light' | 'dark'
    const root = document.documentElement;
    const toggleEl = document.querySelector('.theme-toggle');
    const buttons = toggleEl ? Array.from(toggleEl.querySelectorAll('button[data-mode]')) : [];
    const media = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)');

    function applyTheme(mode) {
        // Clear explicit override first
        root.removeAttribute('data-theme');
        if (mode === 'light' || mode === 'dark') {
            root.setAttribute('data-theme', mode);
        }
        // Update active state
        if (buttons.length) {
            buttons.forEach(b => b.classList.toggle('active', b.dataset.mode === mode));
        }
        // Keep meta color-scheme in sync for form controls/scrollbars
        const meta = document.querySelector('meta[name="color-scheme"]');
        if (meta) {
            if (mode === 'light') meta.setAttribute('content', 'light');
            else if (mode === 'dark') meta.setAttribute('content', 'dark');
            else meta.setAttribute('content', 'light dark');
        }
    }

    function getSaved() {
        try { return localStorage.getItem(THEME_KEY) || 'auto'; } catch { return 'auto'; }
    }

    function save(mode) {
        try { localStorage.setItem(THEME_KEY, mode); } catch {}
    }

    function initTheme() {
        const saved = getSaved();
        applyTheme(saved);
    }

    // Listen to OS changes when in auto
    if (media && media.addEventListener) {
        media.addEventListener('change', () => {
            if (getSaved() === 'auto') {
                applyTheme('auto'); // clears override; CSS media query will take effect
            }
        });
    } else if (media && media.addListener) {
        // Safari/old
        media.addListener(() => {
            if (getSaved() === 'auto') {
                applyTheme('auto');
            }
        });
    }

    if (buttons.length) {
        buttons.forEach(btn => {
            btn.addEventListener('click', () => {
                const mode = btn.dataset.mode; // 'auto' | 'light' | 'dark'
                save(mode);
                applyTheme(mode);
            });
        });
    }

    initTheme();

    // Search Elements
    const searchInput = document.getElementById('search-input');
    const searchBtn = document.getElementById('search-btn');
    const searchBtnMain = document.getElementById('search-btn-main');
    const clearBtn = document.getElementById('clear-btn');

    // Import Elements
    const toggleImportBtn = document.getElementById('toggle-import-btn');
    const importSection = document.getElementById('import-section');
    const importBtn = document.getElementById('import-btn');
    const uploadBtn = document.getElementById('upload-btn');
    const fileInput = document.getElementById('file-input');
    const dropZone = document.getElementById('drop-zone');
    const fileList = document.getElementById('file-list');

    // Results & Loader
    const loader = document.getElementById('loader');
    const results = document.getElementById('results');
    const toastContainer = document.getElementById('toast-container');

    // Toast Notification System
    function showToast(message, type = 'info', duration = 3000) {
        const toast = document.createElement('div');
        toast.className = `toast ${type}`;
        toast.textContent = message;

        toastContainer.appendChild(toast);

        // Trigger reflow
        toast.offsetHeight;

        // Show
        requestAnimationFrame(() => {
            toast.classList.add('show');
        });

        setTimeout(() => {
            toast.classList.remove('show');
            toast.addEventListener('transitionend', () => {
                toast.remove();
            });
        }, duration);
    }

    // Clear button logic
    function toggleClearButton() {
        if (searchInput.value.trim().length > 0) {
            clearBtn.classList.remove('hidden');
        } else {
            clearBtn.classList.add('hidden');
        }
    }

    searchInput.addEventListener('input', toggleClearButton);
    toggleClearButton(); // init

    clearBtn.addEventListener('click', () => {
        searchInput.value = '';
        searchInput.focus();
        toggleClearButton();
    });

    // Keyboard Shortcuts
    document.addEventListener('keydown', (e) => {
        // Focus search on '/'
        if (e.key === '/' && document.activeElement !== searchInput) {
            e.preventDefault();
            searchInput.focus();
        }
        // Clear/Blur on Escape
        if (e.key === 'Escape') {
            if (document.activeElement === searchInput) {
                if (searchInput.value) {
                    searchInput.value = '';
                    toggleClearButton();
                } else {
                    searchInput.blur();
                }
            } else if (document.body.classList.contains('has-results')) {
                // Optional: Go back to home if results are shown?
                // Let's just minimize expanded cards if any first
                const expanded = document.querySelector('.result-item.expanded');
                if (expanded) {
                    // let global click handler do it or trigger explicit
                    // Actually, let's keep it simple: Escape clears focus.
                }
            }
        }
    });

    // Add logo click handler to reset view
    const logo = document.querySelector('.logo');
    if (logo) {
        logo.style.cursor = 'pointer';
        logo.addEventListener('click', () => {
            document.body.classList.remove('has-results');
            results.innerHTML = '';
            // Reset files but keep text if desired, or maybe just go back to "home" mode
            // User requested NOT to clear search text: searchInput.value = '';
            // But we should probably focus it or just leave it
            searchInput.focus();
        });
    }

    let selectedFiles = [];

    // --- SEARCH ---
    function performSearch() {
        const query = searchInput.value.trim();
        if (!query) return;

        // Transition to results view
        document.body.classList.add('has-results');

        loader.style.display = 'block';
        results.innerHTML = '';

        const phraseSearch = document.getElementById('phrase-search').checked;
        const scoringMethod = document.getElementById('scoring-method').value;
        const params = new URLSearchParams({
            q: query,
            phrase: phraseSearch ? '1' : '0',
            scoring: scoringMethod
        });

        fetch(`/api/search?${params.toString()}`)
            .then(response => response.json())
            .then(data => {
                loader.style.display = 'none';
                displayResults(data); // highlight only real matched tokens
            })
            .catch(error => {
                loader.style.display = 'none';
                console.error('Search error:', error);
                showToast('搜索请求失败', 'error');
            });
    }

    searchBtn.addEventListener('click', performSearch);
    searchBtnMain.addEventListener('click', performSearch);
    searchInput.addEventListener('keypress', function (e) {
        if (e.key === 'Enter') {
            performSearch();
        }
    });

    // --- IMPORT ---
    toggleImportBtn.addEventListener('click', () => {
        const isOpen = importSection.classList.toggle('open');
        toggleImportBtn.setAttribute('aria-expanded', String(isOpen));
        importSection.setAttribute('aria-hidden', String(!isOpen));
    });

    // Drag and drop functionality
    dropZone.addEventListener('click', () => fileInput.click());
    dropZone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropZone.classList.add('drag-over');
    });
    dropZone.addEventListener('dragleave', (e) => {
        e.preventDefault();
        dropZone.classList.remove('drag-over');
    });
    dropZone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropZone.classList.remove('drag-over');
        if (e.dataTransfer.files.length > 0) {
            addFiles(e.dataTransfer.files);
        }
    });

    // File selection
    importBtn.addEventListener('click', function (e) {
        e.stopPropagation();
        fileInput.click();
    });

    fileInput.addEventListener('change', function () {
        if (fileInput.files.length > 0) {
            addFiles(fileInput.files);
        }
        // Reset file input to allow selecting the same file(s) again
        fileInput.value = '';
    });

    function addFiles(files) {
        for (const file of files) {
            if (!selectedFiles.some(f => f.name === file.name)) {
                selectedFiles.push(file);
            }
        }
        updateFileListUI();
    }

    function removeFile(fileName) {
        selectedFiles = selectedFiles.filter(f => f.name !== fileName);
        updateFileListUI();
    }

    function updateFileListUI() {
        fileList.innerHTML = '';
        selectedFiles.forEach(file => {
            const fileItem = document.createElement('div');
            fileItem.className = 'file-item';
            fileItem.innerHTML = `
                <span>${file.name}</span>
                <button class="remove-file-btn" data-filename="${file.name}">&times;</button>
            `;
            fileList.appendChild(fileItem);
        });

        document.querySelectorAll('.remove-file-btn').forEach(button => {
            button.addEventListener('click', (e) => {
                removeFile(e.target.dataset.filename);
            });
        });

        uploadBtn.disabled = selectedFiles.length === 0;
    }

    // Uploading files
    uploadBtn.addEventListener('click', function () {
        if (selectedFiles.length === 0) return;

        loader.style.display = 'block';
        uploadBtn.disabled = true;

        // Send all files in a single request; backend enqueues tasks and returns task IDs
        const formData = new FormData();
        selectedFiles.forEach(file => formData.append('file', file));

        fetch('/api/import', {
            method: 'POST',
            body: formData
        })
            .then(response => response.json().catch(() => ({error: 'Invalid JSON response'})))
            .then(data => {
                // Legacy fallback: if backend returns { message: ... } treat as immediate success
                if (data && data.message) {
                    loader.style.display = 'none';
                    showToast('1 个文件导入成功。', 'success');
                    selectedFiles = [];
                    updateFileListUI();
                    uploadBtn.disabled = false;
                    return;
                }

                if (!data || !Array.isArray(data.task_ids)) {
                    loader.style.display = 'none';
                    uploadBtn.disabled = false;
                    console.error('Unexpected response:', data);
                    showToast('导入请求提交失败。', 'error');
                    return;
                }

                const taskIds = data.task_ids;
                // Poll task statuses until all are done
                const finished = new Set(['success', 'failed', 'unsupported']);

                function pollOnce() {
                    return Promise.all(taskIds.map(id =>
                        fetch(`/api/task?id=${encodeURIComponent(id)}`)
                            .then(r => r.json())
                            .catch(() => ({id, status: 'failed', message: '请求失败'}))
                    ));
                }

                function summarize(results) {
                    const ok = results.filter(r => r.status === 'success').length;
                    const fail = results.filter(r => r.status === 'failed').length;
                    const unsup = results.filter(r => r.status === 'unsupported').length;
                    return {ok, fail, unsup};
                }

                function allDone(results) {
                    return results.every(r => finished.has(r.status));
                }

                (function loopPoll(attempt) {
                    pollOnce().then(results => {
                        if (allDone(results) || attempt >= 300) { // ~5分钟超时（1s*300）
                            const {ok, fail, unsup} = summarize(results);
                            loader.style.display = 'none';
                            let msg = `${ok} 个文件导入成功。`;
                            if (fail > 0) msg += ` ${fail} 个失败。`;
                            if (unsup > 0) msg += ` ${unsup} 个格式不支持。`;

                            const type = (fail > 0 || unsup > 0) ? (ok > 0 ? 'info' : 'error') : 'success';
                            showToast(msg, type, 5000);

                            selectedFiles = [];
                            updateFileListUI();
                            uploadBtn.disabled = false;
                        } else {
                            setTimeout(() => loopPoll(attempt + 1), 1000);
                        }
                    }).catch(() => {
                        // polling error, retry with backoff
                        setTimeout(() => loopPoll(attempt + 1), 1500);
                    });
                })(0);
            })
            .catch(error => {
                loader.style.display = 'none';
                uploadBtn.disabled = false;
                console.error('Import error:', error);
                showToast('导入过程中发生错误。', 'error');
            });
    });

    // --- DISPLAY RESULTS ---

    // Remove tokenization-related functions and server settings; backend supplies matched_tokens
    // Escape regex special chars
    function escapeRegex(s) {
        return s.replace(/[.*+?^${}()|[\]\\]/g, '\\\\$&');
    }

    function escapeHtml(str) {
        return str.replace(/[&<>'"]/g, ch => ({
            '&': '&amp;',
            '<': '&lt;',
            '>': '&gt;',
            '\'': '&#39;',
            '"': '&quot;'
        }[ch]));
    }

    function buildRegexFromTokens(tokens, rawQuery) {
        if (!Array.isArray(tokens)) tokens = [];
        let aug = tokens.filter(Boolean);
        const q = (rawQuery || '').trim();
        // 若查询不含空白且长度>=1，作为一个完整候选加入用于整体高亮（覆盖单字/超短查询场景）
        if (q.length >= 1 && !/\s/.test(q)) {
            const lowerSet = new Set(aug.map(t => t && t.toLowerCase()));
            if (!lowerSet.has(q.toLowerCase())) {
                aug.push(q);
            }
        }
        if (!aug.length) return null;
        // 去重
        aug = Array.from(new Set(aug));
        // 按长度降序，保证更长的整体匹配先行，避免被短 token 打断
        aug.sort((a, b) => b.length - a.length);
        return new RegExp('(' + aug.map(escapeRegex).join('|') + ')', 'gi');
    }

    function highlightText(text, regex) {
        if (!regex) return escapeHtml(text || '');
        return escapeHtml(text || '').replace(regex, m => `<mark class="hl">${m}</mark>`);
    }

    function displayResults(data) {
        const rawQuery = searchInput.value; // 当前查询
        results.innerHTML = '';
        const count = Array.isArray(data) ? data.length : 0;
        // Summary line
        const summary = document.createElement('div');
        summary.className = 'results-summary';
        summary.textContent = `共 ${count} 条结果`;
        results.appendChild(summary);
        if (!Array.isArray(data) || data.length === 0) {
            const msg = document.createElement('p');
            msg.textContent = '没有找到匹配的文档';
            results.appendChild(msg);
            return;
        }
        const fragment = document.createDocumentFragment();
        data.forEach((item, idx) => {
            const resultDiv = document.createElement('div');
            resultDiv.className = 'result-item';
            const tokens = Array.isArray(item.matched_tokens) ? item.matched_tokens : [];
            const regex = buildRegexFromTokens(tokens, rawQuery);
            const title = document.createElement('div');
            title.className = 'result-title';
            title.innerHTML = highlightText(item.title, regex);
            const body = document.createElement('div');
            body.className = 'result-body';
            body.innerHTML = highlightText(item.body, regex);
            const score = document.createElement('div');
            score.className = 'result-score';
            score.textContent = `Score: ${Number(item.score).toFixed(4)}`;
            resultDiv.appendChild(title);
            resultDiv.appendChild(body);
            resultDiv.appendChild(score);
            if (tokens.length) {
                const mt = document.createElement('div');
                mt.className = 'matched-tokens';
                mt.textContent = '命中词元：' + Array.from(new Set(tokens)).join(', ');
                resultDiv.appendChild(mt);
            }

            // Initial collapsed height based on computed line-height
            const collapsedLines = 2;

            function computeCollapsedMax(target) {
                const lh = parseFloat(getComputedStyle(target).lineHeight); // px
                return collapsedLines * (isNaN(lh) ? 20 : lh);
            }

            let collapsedMax = computeCollapsedMax(body);
            body.style.maxHeight = `${collapsedMax}px`;

            function toggleExpand(target, expand) {
                const willExpand = expand ?? !resultDiv.classList.contains('expanded');
                target.classList.add('animating');
                // Recompute collapsed height in case of resize/font change
                collapsedMax = computeCollapsedMax(target);
                if (willExpand) {
                    // Add expanded first so scrollHeight reflects full content
                    resultDiv.classList.add('expanded');
                    const startHeight = target.clientHeight; // collapsed height
                    target.style.maxHeight = `${startHeight}px`;
                    // Force reflow
                    target.offsetHeight;
                    const fullHeight = target.scrollHeight;
                    target.style.maxHeight = `${fullHeight}px`;
                    const onEnd = () => {
                        target.classList.remove('animating');
                        // Keep maxHeight so subsequent collapse animates from correct height
                        target.removeEventListener('transitionend', onEnd);
                    };
                    target.addEventListener('transitionend', onEnd);
                } else {
                    // Collapse: keep expanded class until transition ends to avoid abrupt line-clamp jump
                    const fullHeight = target.scrollHeight;
                    target.style.maxHeight = `${fullHeight}px`;
                    target.offsetHeight; // reflow
                    target.style.maxHeight = `${collapsedMax}px`;
                    const onEnd = () => {
                        resultDiv.classList.remove('expanded');
                        target.classList.remove('animating');
                        target.removeEventListener('transitionend', onEnd);
                    };
                    target.addEventListener('transitionend', onEnd);
                }
            }

            // expose for outside click collapse
            resultDiv._toggleExpand = toggleExpand;

            resultDiv.addEventListener('click', (e) => {
                if (e.target && e.target.closest && e.target.closest('a')) return;
                const sel = window.getSelection && window.getSelection();
                if (sel && sel.toString().trim().length > 0) {
                    try {
                        const range = sel.rangeCount ? sel.getRangeAt(0) : null;
                        const container = range && range.commonAncestorContainer;
                        const anchorIn = sel.anchorNode && resultDiv.contains(sel.anchorNode.nodeType === 3 ? sel.anchorNode.parentNode : sel.anchorNode);
                        const focusIn = sel.focusNode && resultDiv.contains(sel.focusNode.nodeType === 3 ? sel.focusNode.parentNode : sel.focusNode);
                        if ((container && resultDiv.contains(container)) || anchorIn || focusIn) {
                            e.stopPropagation();
                            return;
                        }
                    } catch (_) {
                    }
                }
                toggleExpand(body);
            });

            setTimeout(() => {
                resultDiv.classList.add('show');
            }, Math.min(40 * idx, 600));
            fragment.appendChild(resultDiv);
        });
        results.appendChild(fragment);
    }

    // 全局点击：点击结果卡片之外区域时收起所有已展开的卡片（带动画）
    document.addEventListener('click', (e) => {
        if (!e.target.closest('.result-item')) {
            document.querySelectorAll('.result-item.expanded').forEach(card => {
                const body = card.querySelector('.result-body');
                if (card._toggleExpand && body) {
                    card._toggleExpand(body, false);
                }
            });
        }
    });
});
