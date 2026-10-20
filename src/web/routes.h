#pragma once
#include <httplib.h>

namespace cd {
class App;
// Registers the JSON API (/api/...) on the server.
void registerRoutes(httplib::Server& svr, App& app);
}
