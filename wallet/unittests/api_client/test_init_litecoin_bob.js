var net = require('net');

var client = new net.Socket();
client.connect(10010, '127.0.0.1', function () {
    console.log('Connected');
    client.write(JSON.stringify(
		{
		    jsonrpc: '2.0',
		    id: 123,
		    method: 'init_litecoin',
		    params:
			{
			    "ltcUserName": "Bob",
			    "ltcPass": "123",
			    "ltcNodeAddr": "127.0.0.1:13400"
			}
		}) + '\n');
});

client.on('data', function (data) {

    var res = JSON.parse(data);

    console.log("got:", res);

    client.destroy(); // kill client after server's response
});

client.on('close', function () {
    console.log('Connection closed');
});
