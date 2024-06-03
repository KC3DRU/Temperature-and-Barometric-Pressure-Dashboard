
### Block Diagram

```mermaid
flowchart LR;
    subgraph Shed Sensor 
	  B[Env Sensor\nBoard]-.-|IC2\nBus|S[ESP32\nSender Sketch]	
    end
	  S<-.->|LoRa Radio\n900 MHz|R[ESP32\nReceiver Sketch]
    R-->|WiFi|P[Pi4\nRaspian\nMosquitto\nNode-Red]
```
