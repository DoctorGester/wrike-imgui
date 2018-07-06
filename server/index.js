const request = require("request");
const express = require("express");
const app = express();
const arguments = process.argv.slice(2);
const host = arguments.length > 0 ? arguments[1] : 'localhost';

app.use(express.static('out'));

app.get("/wrike/*", (req, res) => {
	req.pipe(request(req.params[0])).pipe(res);
});

app.listen(3637, host, () => console.log("Server started"));
