#include <jni.h>
#include "include/stat.h"


#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


JNIEXPORT jlong JNICALL Java_com_example_scanfs_MainActivity_getAccessTime
  (JNIEnv *env, jobject obj, jstring jFilePath)
{

	const char *filePath = (*env)->GetStringUTFChars(env, jFilePath, NULL);
	struct stat buf; 
	
	if (filePath == NULL) return (jlong)-1;
	if (lstat(filePath, &buf)) {
		return (jlong)-1;
	}
	return time(NULL) - buf.st_atime;
}


