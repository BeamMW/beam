const WebSocket = require('ws');
 
const client = new WebSocket('ws://127.0.0.1:8080');
 
client.on('open', function open() {
	console.log('Connected');
	client.send(JSON.stringify(
		{
			jsonrpc: '2.0',
			id: 123,
			method: 'subscribe',
			params: 
			{
				address: "xxxxxxxxxxxxx",
				privateKey: "xxxxxxxxxxxxxxxxxxx"
			}
		}) + '\n');
});
 
client.on('message', function incoming(data) 
{
	var res = JSON.parse(data);
	console.log('Received:', res);
	client.close();
});

client.on('close', function() 
{
	console.log('Connection closed');
});

