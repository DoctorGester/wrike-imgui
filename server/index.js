const request = require("request");
const express = require("express");
const fs = require("fs");
const app = express();
const arguments = process.argv.slice(2);
const host = arguments.length > 0 ? arguments[1] : 'localhost';

fs.readFile("private.key", "utf8", function(error, private_key) {
    if (error) {
        throw error;
    }

    const key_pattern = "bearer ";

    private_key = private_key.substr(private_key.indexOf(key_pattern) + key_pattern.length);

    app.get("/api/*", (req, res) => {
        const new_request = request("https://www.wrike.com" + req.originalUrl, {
            auth: {
                bearer: private_key
            }
        });

        req.pipe(new_request).pipe(res);
    });
});

app.use(express.static('out'));

app.get("/wrike/*", (req, res) => {
	req.pipe(request(req.params[0])).pipe(res);
});

app.listen(3637, host, () => console.log("Server started"));
