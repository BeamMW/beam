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
				address: "1b9b12a58768fb181b9bea2a1d34d84da8a101d197cdbf4f8fd030fc24808f33450",
				privateKey: "57e7f12544fc4a93e94780abaa84925e12b7d6cd1c8a904bd14aea41a8281940",
				expires: 1234565000
			}
		}) + '\n');
});
 
client.on('message', function incoming(data) 
{
	var res = JSON.parse(data);
	console.log('Received:', res);
//	client.close();
});

client.on('close', function() 
{
	console.log('Connection closed');
});

