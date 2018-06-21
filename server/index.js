const request = require("request");
const express = require("express");
const app = express();

app.use(express.static('out'));

app.get("/wrike/*", (req, res) => {
	req.pipe(request(req.params[0])).pipe(res);
});

app.listen(3637, () => console.log("Server started"));
