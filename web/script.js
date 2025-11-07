document.addEventListener('DOMContentLoaded', function () {
    // Search Elements
    const searchInput = document.getElementById('search-input');
    const searchBtn = document.getElementById('search-btn');
    const searchBtnMain = document.getElementById('search-btn-main');

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

    let selectedFiles = [];

    // --- SEARCH ---
    function performSearch() {
        const query = searchInput.value.trim();
        if (!query) return;

        loader.style.display = 'block';
        results.innerHTML = '';

        fetch(`/api/search?q=${encodeURIComponent(query)}`)
            .then(response => response.json())
            .then(data => {
                loader.style.display = 'none';
                displayResults(data); // highlight only real matched tokens
            })
            .catch(error => {
                loader.style.display = 'none';
                console.error('Search error:', error);
                alert('搜索失败');
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
        importSection.classList.toggle('hidden');
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
                    alert('1 个文件导入成功。');
                    selectedFiles = [];
                    updateFileListUI();
                    uploadBtn.disabled = false;
                    return;
                }

                if (!data || !Array.isArray(data.task_ids)) {
                    loader.style.display = 'none';
                    uploadBtn.disabled = false;
                    console.error('Unexpected response:', data);
                    alert('导入请求提交失败。');
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
                            if (fail > 0) msg += `\n${fail} 个文件导入失败。`;
                            if (unsup > 0) msg += `\n${unsup} 个文件格式不支持。`;
                            alert(msg);
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
                alert('导入过程中发生错误。');
            });
    });

    // --- DISPLAY RESULTS ---

    // Remove tokenization-related functions and server settings; backend supplies matched_tokens
    // Escape regex special chars
    function escapeRegex(s) {
        return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
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

    function buildRegexFromTokens(tokens) {
        if (!Array.isArray(tokens) || !tokens.length) return null;
        const uniq = Array.from(new Set(tokens.filter(Boolean)));
        if (!uniq.length) return null;
        uniq.sort((a, b) => b.length - a.length);
        return new RegExp('(' + uniq.map(escapeRegex).join('|') + ')', 'gi');
    }

    function highlightText(text, regex) {
        if (!regex) return escapeHtml(text || '');
        return escapeHtml(text || '').replace(regex, m => `<mark class="hl">${m}</mark>`);
    }

    function displayResults(data) {
        results.innerHTML = '';
        if (!Array.isArray(data) || data.length === 0) {
            results.innerHTML = '<p>没有找到匹配的文档</p>';
            return;
        }
        data.forEach(item => {
            const resultDiv = document.createElement('div');
            resultDiv.className = 'result-item';
            const tokens = Array.isArray(item.matched_tokens) ? item.matched_tokens : [];
            const regex = buildRegexFromTokens(tokens);
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
            // Toggle expand/collapse only when it's a genuine click, not a text selection or link click
            resultDiv.addEventListener('click', (e) => {
                // Ignore clicks on links inside the card
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
                        // ignore and fall through
                    }
                }
                resultDiv.classList.toggle('expanded');
            });
            results.appendChild(resultDiv);
        });
    }
});
