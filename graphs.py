import pandas as pd
import matplotlib.pyplot as plt



plt.style.use('ggplot')

plt.style.use('ggplot')

cp_r = [2.72242,16.535,22.6499]
std1 = [0.260969,0.848094,2.82528]
ours = [1.54603,14.3143,19.0808]
std2 = [0.480648,1.33765,4.22109]  
  

index = ['20/80', '50/50', '20/80']

df = pd.DataFrame({'cp -r': cp_r, 'Ours Final': ours}, index=index)

ax = df.plot.bar(rot=0, yerr=(std1, std2), capsize=4)
ax.set_xlabel('Ratio of Small/Large Files')
ax.set_ylabel('Average Seconds to Copy')
ax.set_title('Ours Hybrid with Threads Vs. cp -r', fontsize=16)
plt.show()