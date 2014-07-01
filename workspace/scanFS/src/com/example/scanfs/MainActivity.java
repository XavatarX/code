package com.example.scanfs;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.util.*;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.Menu;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import java.io.OutputStream;


public class MainActivity extends Activity {

	String FILE_PATH = "scanned_data";
	static {
		System.loadLibrary("stat");
	}
	public class SystemStats {
		long totalSpace;
		long spaceUsed;
		long sizeUsedInLast5Days;
		long sizeUsedInLast15Days;
		long sizeUsedInLast30Days;
		@Override
		public String toString()
		{
			String str = "totalSpace," + Long.toString(totalSpace) + "\r\n";
			str = str + "spaceUsed," +Long.toString(spaceUsed) + "\r\n";
			str = str + "sizeUsedInLast5Days," +Long.toString(sizeUsedInLast5Days) + "\r\n";
			str = str + "sizeUsedInLast15Days," +Long.toString(sizeUsedInLast15Days) + "\r\n";
			str = str + "sizeUsedInLast30Days," +Long.toString(sizeUsedInLast30Days) + "\r\n";
			return str;
		}
	}
	public native long getAccessTime(String path);
	public  SystemStats stats;
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
        Button btn = (Button) findViewById(R.id.button2);
    	btn.setEnabled(false);
    	FILE_PATH = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS).getAbsolutePath() + "/" + FILE_PATH;
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
    	FileOutputStream fos;
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
        		Log.e(TAG,"Removing");
        		File f = qe.remove();
        		Log.e(TAG,"Removed");
        		if (f.isDirectory()) {
        			currentDirectory = f.getAbsolutePath();
        			File[] list = f.listFiles();
        			if (list == null){
        				continue;
        			}
        			for (File file : list) {
        				Log.e(TAG,"Running---");
        				if (file.isFile()) {
        					Log.e(TAG,"Getting details " + file.getAbsolutePath());
        	    			String s = file.getAbsolutePath() + "," + file.lastModified() + "," + Long.toString(getAccessTime(file.getAbsolutePath())) +"," + file.length();
        	    			Log.e(TAG,"Got");
        	    			try {
        						osw.append(s);
        						Log.e(TAG, s);
        					} catch (IOException e) {
        						// TODO Auto-generated catch block
        						Log.e(TAG, "File write failed: " + e.toString());
        						break;
        					}
        				} else if (file.isDirectory() && !file.getAbsolutePath().contentEquals("/proc") && !file.getAbsolutePath().contentEquals("/sys") && !file.getAbsolutePath().contentEquals("/d") && !file.getAbsolutePath().contentEquals("/dev") && !isSymlink(file)){
        					Log.e(TAG,"Adding dir" + file.getAbsolutePath());
        					qe.add(file);
        					Log.e(TAG,"Added");
        				}
        				updateCounter--;
        				if (updateCounter == 0) {
        					Log.e(TAG,"Publishing");
        					publishProgress(null);
        					Log.e(TAG,"Published");
        					updateCounter = MAX_UPDATE_COUNTER;
        				}
        			}
        			Log.e(TAG,"Files in this directory finished");
        		}
        		try {
        			Log.e(TAG,"Flushing");
    				osw.flush();
    				Log.e(TAG,"Flushed");
    			} catch (IOException e) {
    				// TODO Auto-generated catch block
    				Log.e(TAG, "File Flushing failed");
    				break;
    			}
        	}
        	Log.e(TAG,"Done with all the files");
        	try {
//        		currentDirectory = "Please send file /scanned_data to kanishk.85@gmail.com";
//        		publishProgress(null);
    			osw.close();
    		} catch (IOException e) {
    			// TODO Auto-generated catch block
    			Log.e(TAG, "File Close failed");
    		}
        }
    	@SuppressWarnings("deprecation")
		@Override
        protected String doInBackground(String... params) {
        	File f = new File("/");
        	try {
        		fos=new FileOutputStream(FILE_PATH,false);
                osw = new OutputStreamWriter(fos);

            }
            catch (IOException e) {
                Log.e(TAG, "File Creation failed: " + e.toString());
            } 
        	qe = new LinkedList<File>();
        	qe.add(f);
        	Iterate ();
        	try {
				fos.close();
			} catch (IOException e) {
				// TODO Auto-generated catch block
				Log.e(TAG, "Error in clisoing fos");
			}
    		return "";
    	}
    	@Override
    	protected void onProgressUpdate(Void... values) {
    		TextView txt = (TextView) findViewById(R.id.textView1);
    		txt.setText(currentDirectory);
    	}

    	@Override
    	protected void onPostExecute(String result) {
    		TextView txt = (TextView) findViewById(R.id.textView1);
    		txt.setText("Please send file /scanned_data to kanishk.85@gmail.com");
    		Button btn = (Button) findViewById(R.id.button2);
        	btn.setEnabled(true);
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
    
    @SuppressLint("NewApi")
	public  void email(Context context, String emailTo, String emailCC,
    	    String subject, String emailText,String file)
    	{
    	    //need to "send multiple" to get more than one attachment
    	    final Intent emailIntent = new Intent(android.content.Intent.ACTION_SEND_MULTIPLE);
    	    emailIntent.setType("text/plain");
    	    emailIntent.putExtra(android.content.Intent.EXTRA_EMAIL, 
    	        new String[]{emailTo});
    	    emailIntent.putExtra(android.content.Intent.EXTRA_CC, 
    	        new String[]{emailCC});
    	    emailIntent.putExtra(Intent.EXTRA_SUBJECT, subject); 
    	    emailIntent.putExtra(Intent.EXTRA_TEXT, emailText);
    	    //has to be an ArrayList
    	    ArrayList<Uri> uris = new ArrayList<Uri>();
    	    //convert from paths to Android friendly Parcelable Uri's

    	    File fileIn = new File(file);
    	    fileIn.setReadable(true, false);
    	    Uri u = Uri.fromFile(fileIn);
    	    
    	    uris.add(u);
    	    emailIntent.putParcelableArrayListExtra(Intent.EXTRA_STREAM, uris);
    	    startActivity(emailIntent);
    	}
    public void sendMail(View view) {
    	email(getApplicationContext(), "kanishk.85@gmail.com", "kanishk.85@gmail.com",
    			"Disk usage analysis is attached.",stats.toString(),  FILE_PATH);
    			
    }
    
}
