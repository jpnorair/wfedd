
function get_appropriate_ws_url(extra_url)
{
	var pcol;
	var u = document.URL;

	/*
	 * We open the websocket encrypted if this page came on an
	 * https:// url itself, otherwise unencrypted
	 */

	if (u.substring(0, 5) === "https") {
		pcol = "wss://";
		u = u.substr(8);
	} else {
		pcol = "ws://";
		if (u.substring(0, 4) === "http")
			u = u.substr(7);
	}

	u = u.split("/");

	/* + "/xxx" bit is for IE10 workaround */

	return pcol + u[0] + "/" + extra_url;
}

function new_ws(urlpath, protocol)
{
	return new WebSocket(urlpath, protocol);
}

document.addEventListener("DOMContentLoaded", function() {
	
	var ws = new_ws(get_appropriate_ws_url(""), "otdb");
	try {
		ws.onopen = function() {
		    document.getElementById("path").disabled = 0;
			document.getElementById("req").disabled = 0;
			document.getElementById("send").disabled = 0;
		};
	
		ws.onmessage =function got_packet(msg) {
			document.getElementById("resp").value =
				document.getElementById("resp").value + msg.data + "\n";
			document.getElementById("resp").scrollTop =
				document.getElementById("resp").scrollHeight;
		};
	
		ws.onclose = function(){
		    document.getElementById("path").disabled = 1;
			document.getElementById("req").disabled = 1;
			document.getElementById("send").disabled = 1;
		};
	} catch(exception) {
		alert("<p>Error " + exception);  
	}
	
	function sendmsg()
	{
		ws.send(document.getElementById("req").value);
		document.getElementById("req").value = "";
	}
	
	document.getElementById("send").addEventListener("click", sendmsg);
	
}, false);

