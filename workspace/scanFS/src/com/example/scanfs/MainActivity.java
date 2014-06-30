package com.example.scanfs;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.util.*;
import android.app.Activity;
import android.content.Context;
import android.os.AsyncTask;
import android.os.Bundle;
import android.util.Log;
import android.view.Menu;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

public class MainActivity extends Activity {

	static {
		System.loadLibrary("stat");
	}
	public native long getAccessTime(String path);
	/*
	public native long getAccessTime(String path);
	public Queue<File> qe;
	private static final String TAG = MainActivity.class.getName();
	FileOutputStream fOut;
	OutputStreamWriter osw;
	*/ 
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.main, menu);
        return true;
    }
    private class IterateAsyncTask extends AsyncTask <String, Void, String> {
    	

    	public Queue<File> qe;
    	private static final String TAG = "Scanning";
    	FileOutputStream fOut;
    	OutputStreamWriter osw;
    	String currentDirectory;
    	public boolean isSymlink(File file) {
    		  File canon;
    		  boolean res= false;
    		  try {
    			  if (file.getParent() == null) {
    				  canon = file;
    			  } else {
    				  File canonDir = file.getParentFile().getCanonicalFile();
    				  canon = new File(canonDir, file.getName());
    			  }
    			  res = !canon.getCanonicalFile().equals(canon.getAbsoluteFile());
    		  } catch (Exception e) {
    			  res = true;
    		  }
    		  return res;
    		}
    	private int MAX_UPDATE_COUNTER = 100;
    	void Iterate ()
        {
    		int updateCounter = MAX_UPDATE_COUNTER;
        	while (!qe.isEmpty()) {
        		File f = qe.remove();
        		if (f.isDirectory()) {
        			currentDirectory = f.getAbsolutePath();
        			File[] list = f.listFiles();
        			if (list == null){
        				continue;
        			}
        			for (File file : list) {
        				if (file.isFile()) {
        	    			String s = file.getAbsolutePath() + "," + file.lastModified() + "," + Long.toString(getAccessTime(file.getAbsolutePath())) +"," + file.length();
        	    			try {
        						osw.append(s);
        						Log.e(TAG, s);
        					} catch (IOException e) {
        						// TODO Auto-generated catch block
        						Log.e(TAG, "File write failed: " + e.toString());
        						break;
        					}
        				} else if (file.isDirectory() && !file.getAbsolutePath().contentEquals("/proc") && !file.getAbsolutePath().contentEquals("/sys") && !file.getAbsolutePath().contentEquals("/d") && !file.getAbsolutePath().contentEquals("/dev") && !isSymlink(file)){
        					qe.add(file);
        				}
        				updateCounter--;
        				if (updateCounter == 0) {
        					publishProgress(null);
        					updateCounter = MAX_UPDATE_COUNTER;
        				}
        			}
        		}
        		try {
    				osw.flush();
    			} catch (IOException e) {
    				// TODO Auto-generated catch block
    				Log.e(TAG, "File Flushing failed");
    				break;
    			}
        	}
        	try {
    			osw.close();
    		} catch (IOException e) {
    			// TODO Auto-generated catch block
    			Log.e(TAG, "File Close failed");
    		}
        }
    	@Override
        protected String doInBackground(String... params) {
        	File f = new File("/");
        	try {
                osw = new OutputStreamWriter(openFileOutput("scanned_data", Context.MODE_PRIVATE));

            }
            catch (IOException e) {
                Log.e(TAG, "File Creation failed: " + e.toString());
            } 
        	qe = new LinkedList<File>();
        	qe.add(f);
        	Iterate ();
    		return "";
    	}
    	@Override
    	protected void onProgressUpdate(Void... values) {
    		TextView txt = (TextView) findViewById(R.id.textView1);
    		txt.setText(currentDirectory);
    	}
    }
    
 /*   void Iterate ()
    {
    	while (!qe.isEmpty()) {
    		File f = qe.remove();
    		if (f.isDirectory()) {
    			File[] list = f.listFiles();
    			if (list == null){
    				continue;
    			}
    			for (File file : list) {
    				if (file.isFile()) {
    	    			String s = file.getAbsolutePath() + "," + file.lastModified() + "," + Long.toString(getAccessTime(file.getAbsolutePath())) +"," + file.length();
    	    			try {
    						osw.append(s);
    						Log.e(TAG, s);
    					} catch (IOException e) {
    						// TODO Auto-generated catch block
    						Log.e(TAG, "File write failed: " + e.toString());
    						break;
    					}
    				} else if (file.isDirectory() && !file.getAbsolutePath().contentEquals("/proc") && !file.getAbsolutePath().contentEquals("/sys")){
    					qe.add(file);
    				}
    			}
    		}
    		try {
				osw.flush();
			} catch (IOException e) {
				// TODO Auto-generated catch block
				Log.e(TAG, "File Flushing failed");
				break;
			}
    	}
    	try {
			osw.close();
		} catch (IOException e) {
			// TODO Auto-generated catch block
			Log.e(TAG, "File Close failed");
		}
    }*/
    @SuppressWarnings("deprecation")
	public void StartScan(View view) {
/*    	File f = new File("/");
    	try {
            osw = new OutputStreamWriter(openFileOutput("scanned_data", Context.MODE_PRIVATE));

        }
        catch (IOException e) {
            Log.e(TAG, "File Creation failed: " + e.toString());
        } 
    	qe = new LinkedList<File>();
    	qe.add(f);
    	Iterate ();
    	
        // Do something in response to button
*/
    	new IterateAsyncTask().execute("");
    	Button btn = (Button) view.findViewById(R.id.button1);
    	btn.setEnabled(false);
    	
    }
    
}
