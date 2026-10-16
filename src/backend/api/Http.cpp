#include <backend/api/Http.h>
#include <iostream>

namespace http {

void requireJson(const httplib::Request &request) {
    if (request.get_header_value("Content-Type").rfind("application/json", 0) != 0)
        throw InvalidRequest(415, "Expected Content-Type: application/json");
}

Handler guarded(Authorizer authorizer, Handler handler) {
    return [authorizer = std::move(authorizer), handler = std::move(handler)](
               const httplib::Request &request, httplib::Response &response) {
        if (!authorizer(request.get_header_value("Authorization"))) {
            sendError(response, 401, "Unauthorized");
            return;
        }
        try {
            handler(request, response);
        } catch (const InvalidRequest &e) {
            sendError(response, e.status, e.what());
        } catch (const std::invalid_argument &e) {
            sendError(response, 400, e.what());
        } catch (const std::exception &e) {
            std::cerr << "error: " << request.method << ' ' << request.path << ": " << e.what() << '\n';
            sendError(response, 500, "Internal error");
        }
    };
}

void rejectOtherMethods(httplib::Server &server, const std::string &path, const std::string &allow) {
    const auto reject = [allow](const httplib::Request &, httplib::Response &response) {
        response.set_header("Allow", allow);
        sendError(response, 405, "Method not allowed");
    };
    server.Put(path, reject);
    server.Patch(path, reject);
    server.Delete(path, reject);
}

}
