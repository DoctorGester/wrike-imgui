const request = require("request");
const rp = require("request-promise-native");
const express = require("express");
const session = require("express-session");
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

const api_request = (host, uri, token, refresh_token) => {
    const request_params = {
        auth: {
            bearer: token
        }
    };

    return rp(`https://${host}${uri}`, request_params)
        .catch(error => {
            if (error.statusCode === 401) {
                return rp.post(`https://${host}/oauth2/token`).form({
                    client_id: app_config.client_id,
                    client_secret: app_config.client_secret,
                    grant_type: "refresh_token",
                    refresh_token: refresh_token
                }).then(response => {
                    const new_token = JSON.parse(response.data);

                    req.session.wrike = new_token;

                    return api_request(new_token.host, uri, new_token.access_token, new_token.refresh_token);
                })
            }
        });
};

app.all("/api/*", (req, res) => {
    const wrike = req.session.wrike;

    const new_request = request(`https://${wrike.host}${req.originalUrl}`, {
        auth: {
            bearer: wrike.access_token
        }
    }).on("error", (err) => {
        console.error(err);
    });

    req.pipe(new_request).pipe(res);
});

http2.createServer(options, app).listen(3637, host, () => console.log("Server started"));