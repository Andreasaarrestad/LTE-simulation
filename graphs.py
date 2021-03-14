import untangle
import matplotlib.pyplot as plt
import numpy as np

obj = untangle.parse('corr_flows.xml')

# Flows delay
plotValues = []
for i in range(0,20):
    delayString = obj.FlowMonitor.FlowStats.Flow[i]['lastDelay'].replace("ns","")
    plotValues.append(float(delayString)*10**(-6))

X = np.arange(20)
fig1, ax1 = plt.subplots()
ax1.bar(X,plotValues)
ax1.set_yscale('log')
plt.xlabel('Flow number')
plt.ylabel('ns')
plt.xticks(np.arange(20), np.arange(1,21))
plt.title('Last delay for each flow')
plt.show()


# Flows throughput
plotValues = []
flows = []
for i in range(0,20):
    txBytes = float(obj.FlowMonitor.FlowStats.Flow[i]['txBytes'])
    timeDiff = float(obj.FlowMonitor.FlowStats.Flow[i]['timeLastTxPacket'].replace("ns",""))  - float(obj.FlowMonitor.FlowStats.Flow[i]['timeFirstTxPacket'].replace("ns",""))
    if timeDiff == 0:
        continue
    flows.append(i+1)
    plotValues.append(8*txBytes/timeDiff) 

X = np.arange(len(flows))
fig1, ax1 = plt.subplots()
ax1.bar(X,plotValues)
ax1.set_yscale('log')
plt.xlabel('Flow number')
plt.ylabel('bps')
plt.xticks(np.arange(len(flows)), flows)
plt.title('Total sent throughput for each flow')
plt.show()

