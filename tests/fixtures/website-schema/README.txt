Open Anime Fixture: an offline website for the schema.org website adapter tests
(tests/unit/test_website_providers.cpp). Every page describes its content with
schema.org JSON-LD the way a real site does for search engines. The tests copy
these pages next to AKENO's self-test clips (assets/selftest) and serve them
from a local HTTP server; nothing here is fetched from the internet.

  index.html                   WebSite + SearchAction, an ItemList of four
                               entries, rel="next" to page-2.html
  page-2.html                  the second catalogue page
  search.html                  search results (an ItemList)
  anime/the-signal.html        TVSeries: season 1 inline, season 2 on its own page
  anime/the-signal/season-2.html  TVSeason whose first episode names only its page
  anime/the-signal/s2e1.html   TVEpisode page with its video file
  anime/locked-away.html       episodes marked for subscribers only
  anime/web-player-only.html   episodes offered in a web player only (embedUrl)
  anime/official-embeds.html   episodes with only a YouTube or PeerTube embedUrl
                               (the video-only player), an unknown player, and
                               an episode page naming a PeerTube embed
                               (official-embeds/e4.html); not in the index
  anime/second-page.html       TVSeries with episodes and no seasons
  films/night-train.html       Movie: its video is encrypted, its trailer is not
                               (the trailer must never be played instead)
  plain.html                   a web page without structured data
  organisation.html            structured data that describes no catalogue
