document.addEventListener('DOMContentLoaded', function() {
    // 初始化Material Components
    mdc.textField.MDCTextField.attachTo(document.querySelector('.mdc-text-field'));
    mdc.ripple.MDCRipple.attachTo(document.querySelector('#search-btn'));
    mdc.ripple.MDCRipple.attachTo(document.querySelector('#import-btn'));
    mdc.ripple.MDCRipple.attachTo(document.querySelector('#upload-btn'));

    const searchInput = document.getElementById('search-input');
    const searchBtn = document.getElementById('search-btn');
    const importBtn = document.getElementById('import-btn');
    const uploadBtn = document.getElementById('upload-btn');
    const fileInput = document.getElementById('file-input');
    const fileName = document.getElementById('file-name');
    const progress = document.getElementById('progress');
    const resultsTitle = document.getElementById('results-title');
    const results = document.getElementById('results');

    // 搜索功能
    function performSearch() {
        const query = searchInput.value.trim();
        if (!query) return;

        fetch(`/api/search?q=${encodeURIComponent(query)}`)
            .then(response => response.json())
            .then(data => {
                displayResults(data);
            })
            .catch(error => {
                console.error('Search error:', error);
                alert('搜索失败');
            });
    }

    searchBtn.addEventListener('click', performSearch);
    searchInput.addEventListener('keypress', function(e) {
        if (e.key === 'Enter') {
            performSearch();
        }
    });

    // 导入功能
    importBtn.addEventListener('click', function() {
        fileInput.click();
    });

    fileInput.addEventListener('change', function() {
        const file = fileInput.files[0];
        if (file) {
            fileName.textContent = file.name;
            uploadBtn.disabled = false;
        } else {
            fileName.textContent = '';
            uploadBtn.disabled = true;
        }
    });

    uploadBtn.addEventListener('click', function() {
        const file = fileInput.files[0];
        if (!file) return;

        const formData = new FormData();
        formData.append('file', file);

        progress.style.display = 'block';
        uploadBtn.disabled = true;
        uploadBtn.disabled = true;
        fetch('/api/import', {
            method: 'POST',
            body: formData
        })
        .then(response => response.json())
        .then(data => {
            progress.style.display = 'none';
            uploadBtn.disabled = false;
            if (data.message) {
                alert('导入成功');
            } else {
                alert('导入失败: ' + data.error);
            }
        })
        .catch(error => {
        xhr.onerror = function() {
            progress.style.display = 'none';
            console.error('Import error:', error);
            console.error('Import error:', xhr.statusText);
        };
    });
        xhr.send(formData);
    });
    // 显示搜索结果
    function displayResults(data) {
        results.innerHTML = '';
        if (data.length === 0) {
            resultsTitle.style.display = 'none';
            results.innerHTML = '<p>没有找到匹配的文档</p>';
            return;
        }

        resultsTitle.style.display = 'block';
        data.forEach(item => {
            const resultDiv = document.createElement('div');
            resultDiv.className = 'result-item';
            resultDiv.innerHTML = `
                <div class="result-title">${item.title || '无标题'}</div>
                <div class="result-body">${item.body}</div>
                <div class="result-score">得分: ${item.score.toFixed(2)}</div>
            `;
            results.appendChild(resultDiv);
        });
    }
});
