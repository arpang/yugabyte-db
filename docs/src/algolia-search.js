import algoliasearch from 'algoliasearch';

/* eslint no-underscore-dangle: 0 */
(function () {
  const algoliaAppId = 'UMBCUJCBE8';
  const algoliaApiKey = '7cd0659fb88c5d3f1abce809ad5f96f2';
  const algoliaIndexName = 'yugabytedb_docs';
  const ignoreClickOnMeElement = document.querySelector('body:not(.td-searchpage) .search-area');
  const searchInput = document.getElementById('search-query');

  /**
   * Decode the cookie and return the appropriate cookie value if found
   * Otherwise empty string is returned.
   *
   * return string
   */
  function getCookie(cname) {
    const splittedCookie = document.cookie.split(';');
    const splittedLength = splittedCookie.length;

    let fetchCookie = 0;
    let matchedCookie = '';

    while (fetchCookie < splittedLength) {
      const cookiePair = splittedCookie[fetchCookie].split('=');
      if (cname === cookiePair[0].trim() && cookiePair[1].trim() !== '') {
        matchedCookie = decodeURIComponent(cookiePair[1]);
        break;
      }

      fetchCookie += 1;
    }

    return matchedCookie;
  }

  /**
   * Kapa modal Styling and add shadow on root.
   */
  function askAiPopupStyle() {
    const host = document.querySelector('#kapa-widget-container:not(.styled)');

    if (host) {
      const shadowRoot = host.shadowRoot;
      if (shadowRoot) {
        const style = document.createElement('style');

        // Original (Unminified) file located in `docs/assets/scss/_yb_kapa_modal_styling.scss`.
        style.textContent = `.mantine-SegmentedControl-label{color:#4e5f6d;font-size:13px;font-style:normal;font-weight:400;line-height:16px;display:flex;padding:4px 12px 4px 8px;align-items:center;height:24px;gap: 4px}.mantine-Image-imageWrapper img{border-radius:0}.mantine-SegmentedControl-root span{display:none}.mantine-SegmentedControl-root{padding:0;overflow:visible}.mantine-SegmentedControl-controlActive{gap:4px;background:#fff;border-radius:8px;box-shadow:0 0 0 1px #7879f1}.mantine-SegmentedControl-control:not(:last-child) label:before{content:"";width:24px;height:24px;background:url(/icons/ai-icon.svg) center no-repeat;display:inline-block;filter:grayscale(1) brightness(1.2)}.mantine-SegmentedControl-control:not(:last-child).mantine-SegmentedControl-controlActive label:before{filter:none}.mantine-SegmentedControl-control:not(:last-child).mantine-SegmentedControl-controlActive label{background:linear-gradient(273deg,#ed35ec 5.14%,#ed35c5 38.93%,#7879f1 48.17%,#5e60f0 98.9%);background-clip:text;-webkit-background-clip:text;-webkit-text-fill-color:transparent;font-weight:600}.mantine-SegmentedControl-control:last-child.mantine-SegmentedControl-controlActive label{color:#2b59c3;font-weight:600}.mantine-SegmentedControl-controlActive label{border-radius:8px;border:0;font-size:13px;font-style:normal;font-weight:600;line-height:16px;display:flex;gap:4px}.mantine-SegmentedControl-control:last-child label {padding: 4px 12px;}`;

        shadowRoot.appendChild(style);
      }
      host.classList.add('styled');
    }
  }

  /**
   * Provided name value from the Query param either from URL or with the passed
   * search query.
   */
  function searchURLParameter(name) {
    const fetchFrom = location.search;

    return decodeURIComponent((new RegExp(`[?|&]${name}=([^&;]+?)(&|#|;|$)`).exec(fetchFrom) || [null, ''])[1].replace(/\+/g, '%20')) || null;
  }

  /**
   * Create a URL that contains query, tab, items etc.
   */
  function createSearchUrl(pager = 1) {
    const searchText = encodedSearchedTerm();
    if (history.pushState) {
      let addQueryParams = '';
      if (searchText.length > 0) {
        searchInput.classList.add('have-text');
        addQueryParams = `?query=${searchText.trim()}`;
      } else {
        addQueryParams = '/search/';
        searchInput.classList.remove('have-text');
      }

      if (document.querySelector('body').classList.contains('td-searchpage')) {
        if (pager > 1) {
          addQueryParams += `&page=${pager}`;
        }

        window.history.pushState({}, '', addQueryParams);
      }
    }
  }

  /**
   * Generate Heading ID based on the heading text.
   */
  function generateHeadingIDs(headingText) {
    let headingHash = '';
    let headingId = '';

    headingId = headingText.toLowerCase().trim();
    if (headingId !== '') {
      headingId = headingId.replace(/<em>|<\/em>/g, '')
        .replace(/[^a-zA-Z0-9]/g, '-')
        .replace(/[-]+/g, '-')
        .replace(/^[-]|[-]$/g, '');

      if (headingId !== '') {
        headingHash = `#${headingId}`;
      }
    }

    return headingHash;
  }

  /**
   * Send the click events to Algolia.
   *
   * @param {Object} e - click event.
   */
  function clickEvents(e) {
    if ((e.target.parentNode && e.target.parentNode.className === 'search-title') || (e.target.parentNode.parentNode && e.target.parentNode.parentNode.className === 'search-title')) {
      let clickResult;

      if (e.target.href) {
        clickResult = e.target;
      } else if (e.target.parentNode.href) {
        clickResult = e.target.parentNode;
      }

      let hitPosition = 0;
      let objectId = '';
      let queryId = '';

      if (clickResult.href) {
        if (clickResult.getAttribute('data-objectid')) {
          objectId = clickResult.getAttribute('data-objectid');
        }

        if (clickResult.getAttribute('data-queryid')) {
          queryId = clickResult.getAttribute('data-queryid');
        }

        if (clickResult.getAttribute('data-position')) {
          hitPosition = clickResult.getAttribute('data-position');
        }
      }

      if (objectId !== '' && queryId !== '' && hitPosition > 0) {
        let userTokenValue = getCookie('_ga');
        if (userTokenValue === '') {
          userTokenValue = 'noToken';
        }

        (async () => {
          const rawResponse = await fetch('https://insights.algolia.io/1/events', {
            method: 'POST',
            headers: {
              'Content-Type': 'application/json',
              'X-Algolia-Application-Id': algoliaAppId,
              'X-Algolia-API-Key': algoliaApiKey,
            },
            body: JSON.stringify(
              {
                events: [{
                  eventType: 'click',
                  eventName: 'Link clicked',
                  index: algoliaIndexName,
                  userToken: userTokenValue,
                  queryID: queryId,
                  objectIDs: [objectId],
                  positions: [parseInt(hitPosition, 10)],
                }],
              },
            ),
          });

          await rawResponse.json();
        })();
      }
    }
  }

  /**
   * Show Message when Search is empty.
   */
  function emptySearch() {
    const searchSummary = document.querySelector('#search-summary');
    if (searchSummary) {
      searchSummary.innerHTML = '';
    }

    document.querySelector('.search-result').style.display = 'none';
  }

  /**
   * Encode user searched term to avoid XSS.
   *
   * @returns string
   */
  function encodedSearchedTerm() {
    const searchedValue = searchInput.value.trim();
    const encodedValue = searchedValue.replace(/[\u00A0-\u9999<>&]/g, i => `&#${i.charCodeAt(0)};`);

    return encodedValue;
  }

  /**
   * Main Docs section HTML.
   */
  function docsSection(queryID, hitIs, resultPosition) {
    const searchText = encodedSearchedTerm();

    let content = '';
    hitIs.forEach((hit, index) => {
      const dataPosition = resultPosition + index + 1;
      let pageBreadcrumb = '';
      let pageHash = '';
      let pageTitle = '';
      let subHead = '';

      if (hit.title) {
        pageTitle = hit.title;
      } else if (hit.headers[0]) {
        pageTitle = hit.headers[0];
      } else {
        return;
      }

      if (hit.breadcrumb) {
        pageBreadcrumb = hit.breadcrumb;
      }

      if (hit._highlightResult.title.matchLevel !== 'full' && hit._highlightResult.description.matchLevel !== 'full') {
        let finalSubHead = '';
        let partialHeaderLength = 0;
        let headerFull = '';
        let headerPartial = '';
        if (hit._highlightResult.headers) {
          hit._highlightResult.headers.every(pageHeader => {
            if (pageHeader.matchLevel) {
              const testSubHead = pageHeader.value.replace(/<em>|<\/em>/g, '');
              if (testSubHead.indexOf(searchText) !== -1) {
                finalSubHead = pageHeader.value;

                return false;
              }

              if (pageHeader.matchLevel === 'full' && headerFull === '') {
                headerFull = pageHeader.value;
              } else if (pageHeader.matchLevel === 'partial' && pageHeader.matchedWords.length > partialHeaderLength) {
                headerPartial = pageHeader.value;
                partialHeaderLength = pageHeader.matchedWords.length;
              }
            }

            return true;
          });

          if (finalSubHead !== '') {
            pageHash = generateHeadingIDs(finalSubHead);
            subHead = finalSubHead.replace(/<em>|<\/em>/g, '');
          } else if (headerFull !== '') {
            pageHash = generateHeadingIDs(headerFull);
            subHead = headerFull.replace(/<em>|<\/em>/g, '');
          } else if (headerPartial !== '') {
            pageHash = generateHeadingIDs(headerPartial);
            subHead = headerPartial.replace(/<em>|<\/em>/g, '');
          }
        }
      }

      if (pageTitle === subHead) {
        subHead = '';
      }

      content += `<li>
        <div class="search-title">
          <a href="${hit.url.replace(/^(?:\/\/|[^/]+)*\//, '/')}${pageHash}" data-objectid="${hit.objectID}" data-queryid="${queryID}" data-position="${dataPosition}">
            <span class="search-title-inner">${pageTitle}</span>
            <div class="search-subhead-inner">${subHead}</div>
            <div class="breadcrumb-item">${pageBreadcrumb}</div>
          </a>
        </div>
      </li>`;
    });

    return content;
  }

  /**
   * Search Pagination HTML.
   */
  function searchPager(pagerInfo) {
    document.querySelector('#docs').removeAttribute('style');

    const {
      currentPage, pagerId, pagerType, totalPages,
    } = pagerInfo;
    const searchpager = document.getElementById(pagerId);

    let firstPage = '<span class="pager-btn" data-pager="1">1</span>';
    let lastPage = `<span class="pager-btn" data-pager="${totalPages}">${totalPages}</span>`;
    let nextPageHTML = `<span class="right-btn pager-btn" data-pager="${(currentPage + 1)}"> &raquo; </span>`;
    let pagerClick = '';
    let pagerClickLength = 0;
    let pagerLoop = 0;
    let pagerFull = '';
    let prevPageHTML = `<span class="left-btn pager-btn" data-pager="${(currentPage - 1)}"> &laquo; </span>`;

    if (currentPage === 1) {
      prevPageHTML = '';
      firstPage = '<span class="pager-btn active-page" data-pager="1">1</span>';
    }

    if (currentPage === totalPages) {
      nextPageHTML = '';
      lastPage = `<span class="pager-btn active-page" data-pager="${totalPages}"> ${totalPages}</span>`;
    }

    if (totalPages > 1) {
      let pagerStep = 2;
      let loopTill = totalPages - 1;

      pagerFull = firstPage;

      if (currentPage >= 4) {
        pagerStep = currentPage - 2;
      }

      if (currentPage !== totalPages) {
        if (currentPage === 1 || currentPage === 2 || currentPage === 3) {
          if (totalPages > 6 && currentPage === 1) {
            loopTill = currentPage + 5;
          } else if (totalPages > 6 && currentPage === 2) {
            loopTill = currentPage + 4;
          } else if (totalPages > 6 && currentPage === 3) {
            loopTill = currentPage + 3;
          }
        } else if ((currentPage + 2) < totalPages) {
          loopTill = currentPage + 2;
        } else if ((currentPage + 1) < totalPages) {
          loopTill = currentPage + 1;
        }
      }

      if (pagerStep > 2) {
        pagerFull += '<span class="dots-3">...</span>';
      }

      for (pagerStep; pagerStep <= loopTill; pagerStep += 1) {
        let activeClass = '';
        if (pagerStep === currentPage) {
          activeClass = ' active-page';
        }

        pagerFull += `<span class="pager-btn${activeClass}" data-pager="${pagerStep}">${pagerStep}</span>`;
      }

      if (loopTill < (totalPages - 1)) {
        pagerFull += '<span class="dots-3">...</span>';
      }

      pagerFull += lastPage;
    }

    searchpager.innerHTML = `<nav>
      <div class="pager-area">
        <div class="left-right-button" data-pager-type="${pagerType}">
          ${prevPageHTML}
          <span class="page-number">${pagerFull}</span>
          ${nextPageHTML}
        </div>
      </div>
    </nav>
    <span class="left-right-button mobile-button" data-pager-type="${pagerType}"></span>`;

    pagerClick = document.querySelectorAll(`#${pagerId} .left-right-button .pager-btn`);
    pagerClickLength = pagerClick.length;

    while (pagerLoop < pagerClickLength) {
      pagerClick[pagerLoop].addEventListener('click', searchSettings);
      pagerLoop += 1;
    }
  }

  /**
   * Shoe "Ask AI" button in search results dropdown.
   */
  function kapaAskAI() {
    const askAI = document.getElementById('yb-ask-ai');
    if (askAI) {
      askAI.addEventListener('click', () => {
        if (window.Kapa) {
          window.Kapa.open({
            mode: 'ai',
            query: encodedSearchedTerm(),
            submit: true,
          });
        }
        askAiPopupStyle();
      });
    }
    const askAITop = document.querySelector('.ask-ai-top span');
    if (askAITop) {
      askAITop.addEventListener('click', () => {
        askAiPopupStyle();
      });
    }
  }

  /**
   * Trigger AI Search on clicking custom button.
   */
  function kapaSearch() {
    const aiSearch = document.getElementById('ai-search');
    if (aiSearch) {
      aiSearch.addEventListener('click', () => {
        if (window.Kapa) {
          window.Kapa.open({
            mode: 'search',
            query: encodedSearchedTerm(),
            submit: true,
          });
        }
        askAiPopupStyle();
      });
    }
  }

  /**
   * Add queries with filters selected by user and call search algolia function.
   */
  function searchAlgolia() {
    const searchedTerm = encodedSearchedTerm();

    let searchValue = searchedTerm;
    if (searchValue.length > 0) {
      document.querySelector('.search-result').style.display = 'block';
      setTimeout(() => {
        document.querySelector('.search-result').style.display = 'block';
      }, 800);
    } else {
      emptySearch();
      setTimeout(emptySearch, 800);
      return;
    }

    let perPageCount = 8;
    if (document.querySelector('body').classList.contains('td-searchpage')) {
      perPageCount = 10;
    }

    const client = algoliasearch(algoliaAppId, algoliaApiKey);
    const index = client.initIndex(algoliaIndexName);
    const pageItems = searchURLParameter('page');
    const searchpagerparent = document.querySelector('#pagination-docs');
    const searchOptions = {
      hitsPerPage: perPageCount,
      page: 0,
      clickAnalytics: true,
    };
    const searchSummary = document.getElementById('search-summary');

    if (searchValue.includes('_')) {
      const searchScript = document.getElementById('algolia-search-script');
      const sequenceExpressions = searchScript.getAttribute('data-sequence-expressions');

      if (sequenceExpressions) {
        searchValue = searchValue.replace(/_/g, '-');
      }
    }

    if (pageItems && pageItems > 0) {
      searchOptions.page = pageItems - 1;
    }

    index.search(searchValue, searchOptions).then(
      ({
        hits, nbHits, nbPages, page, queryID,
      }) => {
        const searchResults = document.getElementById('doc-hit');
        const startResultPosition = page * perPageCount;

        let pagerDetails = {};
        let sectionHTML = '';
        let totalResults = nbHits;
        sectionHTML += docsSection(queryID, hits, startResultPosition);

        if (totalResults > 1000) {
          totalResults = 1000;
        }

        document.getElementById('doc-hit').innerHTML = sectionHTML;

        if (hits.length > 0 && sectionHTML !== '') {
          if (searchSummary !== null) {
            searchSummary.innerHTML = `${totalResults} results found for <b>"${searchedTerm}"</b>. <a role="button" id="ai-search">Try this search in AI</a>.`;
          }

          const btnSearchVal = document.querySelector('.yb-kapa-button .search-val');
          if (btnSearchVal) {
            btnSearchVal.innerHTML = `${searchedTerm}`;
          }

          setTimeout(() => {
            if (document.querySelector('.yb-kapa-button')) {
              document.querySelector('.yb-kapa-button').style.display = 'block';
            }
          }, 500);
        } else {
          const noResultMessage = `No results found for <b>"${searchedTerm}"</b>. <a role="button" id="ai-search">Try this search in AI</a>.`;
          if (searchSummary) {
            searchSummary.innerHTML = noResultMessage;
          } else {
            document.getElementById('doc-hit').innerHTML = `<li class="no-result">${noResultMessage}</li>`;
          }

          if (document.querySelector('.yb-kapa-button')) {
            document.querySelector('.yb-kapa-button').style.display = 'none';
          }
        }

        kapaSearch();

        if (document.querySelector('body').classList.contains('td-searchpage')) {
          pagerDetails = {
            currentPage: page + 1,
            pagerId: 'pagination-docs',
            pagerType: 'docs',
            totalHits: totalResults,
            totalPages: nbPages,
          };

          searchPager(pagerDetails);
          searchpagerparent.className = `pager results-${totalResults}`;
        } else {
          searchpagerparent.className = `pager results-${totalResults}`;
          let viewAll = '';
          if (nbPages > 1) {
            viewAll = `<a href="/search/?query=${searchValue}" title="View all results">View all results <img src="/icons/arrow-btn-sm.svg"></a>`;
          }

          document.getElementById('pagination-docs').innerHTML = `<nav class="pager-area">
            <div class="pager-area">
              <span class="total-result">${totalResults} Results</span>
            </div>
            ${viewAll}
          </nav>`;
        }

        if (searchResults) {
          searchResults.addEventListener('click', clickEvents);
          searchResults.addEventListener('contextmenu', clickEvents);
        }
      },
    );
  }

  /**
   * Runs on keyup or filters click or Pagination click function.
   */
  function searchSettings(e) {
    if (e.type === 'click') {
      if (e.target.classList.contains('pager-btn') && !e.target.classList.contains('disabled')) {
        if (e.target.getAttribute('data-pager')) {
          if (document.querySelector('body').classList.contains('td-searchpage')) {
            createSearchUrl(e.target.getAttribute('data-pager'));
          }

          searchAlgolia();
          setTimeout(() => {
            window.scrollTo(0, 0);
          }, 800);
        }
      }
    } else {
      createSearchUrl();
      searchAlgolia();
    }
  }

  /**
   * This function attached all the events which is needed for the search to work
   * properly.
   */
  function addSearchEvents() {
    if (searchURLParameter('query') && document.querySelector('body').classList.contains('td-searchpage')) {
      searchInput.setAttribute('value', searchURLParameter('query'));
    }

    document.addEventListener('DOMContentLoaded', () => {
      searchAlgolia();
    });

    // Events binding.
    searchInput.addEventListener('keyup', searchSettings);
    searchInput.addEventListener('paste', searchSettings);
  }

  addSearchEvents();
  kapaAskAI();

  document.addEventListener('keydown', (event) => {
    if (event.target.nodeName === 'TEXTAREA') {
      return;
    }

    if (event.key === '/' || event.code === 'NumpadDivide') {
      document.querySelector('body').classList.add('search-focus');
      if (searchURLParameter('query')) {
        searchInput.setAttribute('value', searchURLParameter('query'));
      } else {
        searchInput.setAttribute('value', '');
      }

      setTimeout(() => {
        searchInput.focus();
        searchInput.select();
      }, 200);
    }
  });

  if (ignoreClickOnMeElement) {
    document.addEventListener('click', (event) => {
      const isClickInsideElement = ignoreClickOnMeElement.contains(event.target);
      if (!isClickInsideElement) {
        emptySearch();
      }
    });
  }

  const inputReset = document.querySelector('.reset-input');
  inputReset.addEventListener('click', () => {
    searchInput.value = '';
    emptySearch();
    searchInput.classList.remove('have-text');
    if (document.querySelector('body').classList.contains('td-searchpage')) {
      window.history.pushState({}, '', '/search/');
    }
  });
})();
