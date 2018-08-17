const request = require("request");
const rp = require("request-promise-native");
const express = require("express");
const session = require("express-session");
const body_parser = require("body-parser");
const path = require("path");
const process = require("process");
const http2 = require("spdy");
const fs = require("fs");

const app = express();
const arguments = process.argv.slice(2);
const host = arguments.length > 0 ? arguments[1] : "localhost";
const app_config = JSON.parse(fs.readFileSync("app_config.json", "utf8"));

const options = {
    key: fs.readFileSync("localhost.key", "utf8"),
    cert: fs.readFileSync("localhost.crt", "utf8")
};

const redirect = (response, to) => {
    response.writeHead(302, { Location: to });
    response.end();
};

const file = (name) => {
    return path.join(process.cwd(), "out", name);
};

const session_params = {
    cookie: {
        secure: true,
        maxAge: 365 * 24 * 60 * 60 * 1000
    },
    secret: "meat grinder",
    name: "wrike imgui",
    resave: false,
    saveUninitialized: false
};

if (app.get("env") === "development") {
    console.log("Using session file store");

    const file_store = require("session-file-store")(session);
    const updated_params = Object.assign({
        store: new file_store({})
    }, session_params);

    app.use(session(updated_params));
} else {
    console.log("Using session memory store");

    const memory_store = require("memorystore")(session);
    const updated_params = Object.assign({
        // prune expired entries every 24h
        store: new memory_store({ checkPeriod: 86400000 })
    }, session_params);

    app.use(session(updated_params));
}

app.use(body_parser.json());
app.use(express.static("out"));

app.get("/login", (req, res) => {
    console.log("Displaying login page");
    res.sendFile(file("login.html"));
});

app.get("/authorize", (req, res) => {
    console.log("Authorize redirect received");

    if (req.query.error) {
        res.write(`AUTH ERROR: ${req.query.error}: ${req.query.error_description}`);
        res.end();
        return;
    }

    if (req.query.code) {
        console.log("Requesting access token");

        request.post("https://www.wrike.com/oauth2/token", (error, response, body) => {
            console.log("Authorized at " + new Date().toString());

            if (error) {
                console.error(error);
                return;
            }

            req.session.wrike = JSON.parse(body);

            redirect(res, "/workspace");
        }).form({
            client_id: app_config.client_id,
            client_secret: app_config.client_secret,
            grant_type: "authorization_code",
            code: req.query.code
        });
    }
});

app.use((req, res, next) => {
    if(!req.session.wrike) {
        redirect(res, "/login");
    } else {
        next();
    }
});

app.get("/logout", (req, res) => {
    console.log("Logging out");

    req.session.destroy();

    redirect(res, "/login");
});

app.get("/workspace", (req, res) => {
    res.sendFile(file("wrike.html"));
});

app.get("/wrike/*", (req, res) => {
    const new_request = request(req.params[0]).on("error", (err) => {
        console.error(err);
    });

	req.pipe(new_request).pipe(res);
});

let refresh_token_promise;

const do_refresh_token = (host, refresh_token, session) => {
    if (!refresh_token_promise) {
        console.log("Access token is being refreshed");

        refresh_token_promise = rp.post(`https://${host}/oauth2/token`).form({
            client_id: app_config.client_id,
            client_secret: app_config.client_secret,
            grant_type: "refresh_token",
            refresh_token: refresh_token
        }).then(response => {
            const new_token = JSON.parse(response);
            session.wrike.access_token = new_token.access_token;

            refresh_token_promise = undefined;

            return new_token;
        });
    }

    return refresh_token_promise;
};

const api_request = (host, uri, token, refresh_token, session) => {
    const request_params = {
        uri: `https://${host}${uri}`,
        auth: {
            bearer: token
        }
    };

    return rp(request_params)
        .catch(error => {
            if (error.statusCode === 401) {
                console.log(`Caught 401 in ${uri}`);

                return do_refresh_token(host, refresh_token, session).then(new_token => {
                    console.log(`Got new token for ${uri}`);
                    return api_request(host, uri, new_token.access_token, new_token.refresh_token, session);
                });
            } else {
                throw error;
            }
        });
};

app.all("/api/*", (req, res) => {
    const wrike = req.session.wrike;

    api_request(wrike.host, req.originalUrl, wrike.access_token, wrike.refresh_token, req.session)
        .then(response => {
            res
                .type('application/json')
                .send(response);
        })
        .catch(error => {
            res
                .status(error.statusCode)
                .type('application/json')
                .send(error.body);
        });
});

http2.createServer(options, app).listen(3637, host, () => console.log("Server started"));