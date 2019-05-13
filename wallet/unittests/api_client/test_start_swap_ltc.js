var net = require('net');

var client = new net.Socket();
client.connect(10000, '127.0.0.1', function () {
    console.log('Connected');
    client.write(JSON.stringify(
		{
		    jsonrpc: '2.0',
		    id: 123,
		    method: 'start_swap',
		    params:
			{
			    "amount": 1000,
			    "fee": 10,
			    "swapAmount": 90000,
			    "swapCoin": "ltc",
			    "beamSide": true,
			    "address": "290146b2d32d2c83690ceeb8f2da41fb892a61130e01f298b216424d39552690431"
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
