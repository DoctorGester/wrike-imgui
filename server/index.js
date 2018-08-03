const request = require("request");
const express = require("express");
const http2 = require('spdy');
const fs = require("fs");
const app = express();
const arguments = process.argv.slice(2);
const host = arguments.length > 0 ? arguments[1] : 'localhost';

const options = {
    key: fs.readFileSync("localhost.key", "utf8"),
    cert: fs.readFileSync("localhost.crt", "utf8")
};

const key_pattern = "bearer ";
const key_file = fs.readFileSync("private.key", "utf8");
const private_key = key_file.substr(key_file.indexOf(key_pattern) + key_pattern.length);

app.use(express.static('out'));

app.get("/wrike/*", (req, res) => {
    const new_request = request(req.params[0]).on('error', function(err) {
        console.error(err);
    });

	req.pipe(new_request).pipe(res);
});

app.get("/api/*", (req, res) => {
    const new_request = request("https://www.wrike.com" + req.originalUrl, {
        auth: {
            bearer: private_key
        }
    }).on('error', function(err) {
        console.error(err);
    });

    req.pipe(new_request).pipe(res);
});

http2.createServer(options, app).listen(3637, host, () => console.log("Server started"));
