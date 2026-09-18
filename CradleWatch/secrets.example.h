// Copy this file to secrets.h (which is gitignored) and fill in your values.
#pragma once

#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

// Cradlewise web dashboard -> Cradlewise Data API -> Generate.
// Tokens are valid for 60 days; the dashboard shows a warning banner on the
// device when it expires (the API returns 401).
#define CW_TOKEN "cw_your_token_here"

// POSIX TZ string for the nursery's timezone (used for the clock, night mode,
// and the day-metrics date range). Examples:
//   Pacific:  "PST8PDT,M3.2.0,M11.1.0"
//   Eastern:  "EST5EDT,M3.2.0,M11.1.0"
//   Central Europe: "CET-1CEST,M3.5.0,M10.5.0/3"
#define TZ_STRING "PST8PDT,M3.2.0,M11.1.0"

// Last resort only: skip TLS certificate validation if both bundled root CAs
// ever stop matching the server. Leave commented out.
// #define CW_TLS_INSECURE
